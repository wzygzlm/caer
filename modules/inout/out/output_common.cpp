/*
 * Here we handle all outputs in a common way, taking in event packets
 * as input and writing a byte buffer to a stream as output.
 * The main-loop part is responsible for gathering the event packets,
 * copying them and their events (valid or not depending on configuration),
 * and putting them on a transfer ring-buffer. A second thread, called the
 * output handler, gets the packet groups from there, orders them according
 * to the AEDAT 3.X format specification, and breaks them up into chunks as
 * directed to write them to a file descriptor efficiently (buffered I/O).
 * The AEDAT 3.X format specification specifically states that there is no
 * relation at all between packets from different sources at the output level,
 * that they behave as if independent, which we do here to simplify the system
 * considerably: one output module (or Sink) can only work with packets from
 * one source. Multiple sources will have to go to multiple output modules!
 * The other stipulation in the AEDAT 3.X specifications is on ordering of
 * events from the same source: the first timestamp of a packet determines
 * its order in the packet stream, from smallest timestamp to largest, which
 * is the logical monotonic increasing time ordering you'd expect.
 * This kind of ordering is useful and simplifies reading back data later on;
 * if you read a packet of type A with TS A-TS1, when you next read a packet of
 * the same type A, with TS A-TS2, you know you must also have read all other
 * events, of this AND all other present types, with a timestamp between A-TS1
 * and (A-TS2 - 1). This makes time-based reading and replaying of data very easy
 * and efficient, so time-slice playback or real-time playback get relatively
 * simple to implement. Data-amount based playback is always relatively easy.
 *
 * Now, outputting event packets in this particular order from an output module
 * requires some additional processing: before you can write out packet A with TS
 * A-TS1, you need to be sure no other packets with a timestamp smaller than
 * A-TS1 can come afterwards (the only solution would be to discard them at
 * that point to maintain the correct ordering, and you'd want to avoid that).
 * We cannot assume a constant and quick data flow, since at any point during a
 * recording, data producers can be turned off, packet size etc. configuration
 * changed, or some events, like Special ones, are rare to begin with during
 * normal camera operation (for example the TIMESTAMP_WRAP every 35 minutes).
 * But we'd like to write data continuously and as soon as possible!
 * Thankfully cAER/libcaer come to the rescue due to a small but important
 * detail of how input modules are implemented (input modules are all those
 * modules that create new data in some way, also called a Source).
 * They either create sequences of single packets, where the ordering is trivial,
 * or so called 'Packet Containers', which do offer timestamp-related guarantees.
 * From the libcaer/events/packetContainer.h documentation:
 *
 * "An EventPacketContainer is a logical construct that contains packets
 * of events (EventPackets) of different event types, with the aim of
 * keeping related events of differing types, such as DVS and IMU data,
 * together. Such a relation is usually based on time intervals, trying
 * to keep groups of event happening in a certain time-slice together.
 * This time-order is based on the *main* time-stamp of an event, the one
 * whose offset is referenced in the event packet header and that is
 * used by the caerGenericEvent*() functions. It's guaranteed that all
 * conforming input modules keep to this rule, generating containers
 * that include all events from all types within the given time-slice."
 *
 * Understanding this gives a simple solution to the problem above: if we
 * see all the packets contained in a packet container, which is the case
 * for each run through of the cAER mainloop (as it fetches *one* new packet
 * container each time from an input module), we can order the packets of
 * the container correctly, and write them out to a file descriptor.
 * Then we just rinse and repeat for every new packet container.
 * The assumption of one run of the mainloop getting at most one packet
 * container from each Source is correct with the current implementation,
 * and future designs of Sources should take this into account! Delays in
 * packet containers getting to the output module are still allowed, provided
 * the ordering doesn't change and single packets aren't mixed, which is
 * a sane restriction to impose anyway.
 */

#include "output_common.hpp"
#include "caer-sdk/mainloop.h"
#include "caer-sdk/cross/portable_threads.h"
#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/buffers.h"

#ifdef ENABLE_INOUT_PNG_COMPRESSION
#include <png.h>
#endif

#include <libcaercpp/events/packetContainer.hpp>
#include <libcaercpp/events/frame.hpp>
#include <libcaercpp/events/special.hpp>

#include <fstream>

namespace cevt = libcaer::events;

static void caerOutputCommonConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

/**
 * ============================================================================
 * MAIN THREAD
 * ============================================================================
 * Handle Run and Reset operations on main thread. Data packets are copied into
 * the transferRing for processing by the compressor thread.
 * ============================================================================
 */
static void copyPacketsToTransferRing(outputCommonState state, const cevt::EventPacketContainer &packetsContainer);

void caerOutputCommonRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	outputCommonState state = (outputCommonState) moduleData->moduleState;

	const cevt::EventPacketContainer container(in, false);

	copyPacketsToTransferRing(state, container);
}

void caerOutputCommonReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	outputCommonState state = (outputCommonState) moduleData->moduleState;

	if (resetCallSourceID == state->sourceID.load(std::memory_order_relaxed)) {
		// The timestamp reset call came in from the Source ID this output module
		// is responsible for, so we ensure the timestamps are reset and that the
		// special event packet goes out for sure.

		// Send lone packet container with just TS_RESET.
		// Allocate packet container just for this event.
		caerEventPacketContainer tsResetContainer = caerEventPacketContainerAllocate(1);
		if (tsResetContainer == nullptr) {
			caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Failed to allocate tsReset event packet container.");
			return;
		}

		// Allocate special packet just for this event.
		caerSpecialEventPacket tsResetPacket = caerSpecialEventPacketAllocate(1, resetCallSourceID,
			I32T(state->lastTimestamp >> 31));
		if (tsResetPacket == nullptr) {
			caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Failed to allocate tsReset special event packet.");
			return;
		}

		// Create timestamp reset event.
		caerSpecialEvent tsResetEvent = caerSpecialEventPacketGetEvent(tsResetPacket, 0);
		caerSpecialEventSetTimestamp(tsResetEvent, INT32_MAX);
		caerSpecialEventSetType(tsResetEvent, TIMESTAMP_RESET);
		caerSpecialEventValidate(tsResetEvent, tsResetPacket);

		// Assign special packet to packet container.
		caerEventPacketContainerSetEventPacket(tsResetContainer, SPECIAL_EVENT, (caerEventPacketHeader) tsResetPacket);

		while (!caerRingBufferPut(state->compressorRing, tsResetContainer)) {
			; // Ensure this goes into the first ring-buffer.
		}

		// Reset timestamp checking.
		state->lastTimestamp = 0;
	}
}

/**
 * Copy event packets to the ring buffer for transfer to the output handler thread.
 *
 * @param state output module state.
 * @param packetsContainer a container with all the event packets to send out.
 */
static void copyPacketsToTransferRing(outputCommonState state, const cevt::EventPacketContainer &packetsContainer) {
	std::vector<std::shared_ptr<const cevt::EventPacket>> packets;

	// Count how many packets are really there, skipping empty event packets.
	for (auto &packet : packetsContainer) {
		// Found non-empty event packet.
		if (packet) {
			// Get source information from the event packet.
			int16_t eventSource = packet->getEventSource();

			// Check that source is unique.
			int16_t sourceID = state->sourceID.load(std::memory_order_relaxed);

			if (sourceID == -1) {
				sshsNode sourceInfoNode = caerMainloopGetSourceInfo(eventSource);
				if (sourceInfoNode == nullptr) {
					// This should never happen, but we handle it gracefully.
					caerModuleLog(state->parentModule, CAER_LOG_ERROR,
						"Failed to get source info to setup output module.");
					return;
				}

				state->sourceInfoString = sshsNodeGetStdString(sourceInfoNode, "sourceString");

				state->sourceID.store(eventSource); // Remember this!
			}
			else if (sourceID != eventSource) {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR,
					"An output module can only handle packets from the same source! "
						"A packet with source %" PRIi16 " was sent, but this output module expects only packets from source %" PRIi16 ".",
					eventSource, sourceID);
				continue;
			}

			// Source ID is correct, packet is not empty, we got it!
			packets.push_back(packet);
		}
	}

	// There was nothing in this mainloop run!
	if (packets.empty()) {
		return;
	}

	// Filter out the TS_RESET packet, as we ensure that that one is always present in the
	// caerOutputCommonReset() function, so that even if the special event stream is not
	// output/captured by this module, the TS_RESET event will be present in the output.
	// The TS_RESET event would be alone in a packet that is also the only one in its
	// packetContainer/mainloop cycle, so we can check for this very efficiently.
	if ((packets.size() == 1)
		&& (packets[0]->size() == 1)
		&& (packets[0]->getEventType() == SPECIAL_EVENT)
		&& ((*std::dynamic_pointer_cast<cevt::SpecialEventPacket>(packets[0]))[0].getType() == TIMESTAMP_RESET)) {
		return;
	}

	// Allocate memory for event packet array structure that will get passed to output handler thread.
	// We use C-style structure henceforth because we work directly on the underlying memory.
	caerEventPacketContainer eventPackets = caerEventPacketContainerAllocate((int32_t) packets.size());
	if (eventPackets == nullptr) {
		return;
	}

	// Handle the valid only flag here, that way we don't have to do another copy and
	// process it in the output handling thread. We get the value once here, so we do
	// the same for all packets from the same mainloop run, avoiding mid-way changes.
	bool validOnly = state->validOnly.load(std::memory_order_relaxed);

	// Now copy each event packet and send the array out. Track how many packets there are.
	size_t idx = 0;
	int64_t highestTimestamp = 0;

	for (auto &packet : packets) {
		if ((validOnly && (packet->getEventValid() == 0))
			|| (!validOnly && (packet->getEventNumber() == 0))) {
			caerModuleLog(state->parentModule, CAER_LOG_NOTICE,
				"Submitted empty event packet to output. Ignoring empty event packet.");
			continue;
		}

		int64_t cpFirstEventTimestamp = packet->genericGetEvent(0).getTimestamp64();

		if (cpFirstEventTimestamp < state->lastTimestamp) {
			// Smaller TS than already sent, illegal, ignore packet.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR,
				"Detected timestamp going back, expected at least %" PRIi64 " but got %" PRIi64 "."
				" Ignoring packet of type %" PRIi16 " from source %" PRIi16 ", with %" PRIi32 " events!",
				state->lastTimestamp, cpFirstEventTimestamp, packet->getEventType(),
				packet->getEventSource(), packet->size());
			continue;
		}
		else {
			// Bigger or equal TS than already sent, this is good. Strict TS ordering ensures
			// that all other packets in this container are the same.
			// Update highest timestamp for this packet container, based upon its valid packets.
			int64_t cpLastEventTimestamp = packet->genericGetEvent(-1).getTimestamp64();

			if (cpLastEventTimestamp > highestTimestamp) {
				highestTimestamp = cpLastEventTimestamp;
			}
		}

		try {
			if (validOnly) {
				caerEventPacketContainerSetEventPacket(eventPackets, (int32_t) idx, (caerEventPacketHeader)
					packet->copy(cevt::EventPacket::copyTypes::VALID_EVENTS_ONLY).release());
			}
			else {
				caerEventPacketContainerSetEventPacket(eventPackets, (int32_t) idx, (caerEventPacketHeader)
					packet->copy(cevt::EventPacket::copyTypes::EVENTS_ONLY).release());
			}

			idx++;
		}
		catch (const std::bad_alloc &) {
			// Failed to copy packet. Signal but try to continue anyway.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to copy event packet to output.");
		}
	}

	// We might have failed to copy all packets (unlikely), or skipped all of them
	// due to timestamp check failures.
	if (idx == 0) {
		caerEventPacketContainerFree(eventPackets);

		return;
	}

	// Remember highest timestamp for check in next iteration. Only update
	// if we actually got any packets through.
	state->lastTimestamp = highestTimestamp;

	// Reset packet container size so we only consider the packets we managed
	// to successfully copy.
	caerEventPacketContainerSetEventPacketsNumber(eventPackets, (int32_t) idx);

	retry: if (!caerRingBufferPut(state->compressorRing, eventPackets)) {
		if (state->keepPackets.load(std::memory_order_relaxed)) {
			// Delay by 500 µs if no change, to avoid a wasteful busy loop.
			std::this_thread::sleep_for(std::chrono::microseconds(500));

			// Retry forever if requested.
			goto retry;
		}

		caerEventPacketContainerFree(eventPackets);

		caerModuleLog(state->parentModule, CAER_LOG_NOTICE,
			"Failed to put packet's array copy on transfer ring-buffer: full.");
	}
}

/**
 * ============================================================================
 * COMPRESSOR THREAD
 * ============================================================================
 * Handle data ordering, compression, and filling of final byte buffers, that
 * will be sent out by the Output thread.
 * ============================================================================
 */
static void compressorThread(outputCommonState state);

static void orderAndSendEventPackets(outputCommonState state, caerEventPacketContainer currPacketContainer);
static int packetsFirstTimestampThenTypeCmp(const void *a, const void *b);
static void sendEventPacket(outputCommonState state, caerEventPacketHeader packet);
static size_t compressEventPacket(outputCommonState state, caerEventPacketHeader packet, size_t packetSize);
static size_t compressTimestampSerialize(outputCommonState state, caerEventPacketHeader packet);

#ifdef ENABLE_INOUT_PNG_COMPRESSION
static void caerLibPNGWriteBuffer(png_structp png_ptr, png_bytep data, png_size_t length);
static size_t compressFramePNG(outputCommonState state, caerEventPacketHeader packet);
#endif

static void compressorThread(outputCommonState state) {
	// Set thread name.
	std::string threadName = state->parentModule->moduleSubSystemString;
	threadName += "[Compressor]";
	portable_thread_set_name(threadName.c_str());

	// If no data is available on the transfer ring-buffer, sleep for 1 ms.
	// to avoid wasting resources in a busy loop.
	while (state->running.load(std::memory_order_relaxed)) {
		// Get the newest event packet container from the transfer ring-buffer.
		caerEventPacketContainer currPacketContainer = (caerEventPacketContainer) caerRingBufferGet(state->compressorRing);
		if (currPacketContainer == nullptr) {
			// There is none, so we can't work on and commit this.
			// We just sleep here a little and then try again, as we need the data!
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		// Respect time order as specified in AEDAT 3.X format: first event's main
		// timestamp decides its ordering with regards to other packets. Smaller
		// comes first. If equal, order by increasing type ID as a convenience,
		// not strictly required by specification!
		orderAndSendEventPackets(state, currPacketContainer);
	}

	// Handle shutdown, write out all content remaining in the transfer ring-buffer.
	caerEventPacketContainer packetContainer;
	while ((packetContainer = (caerEventPacketContainer) caerRingBufferGet(state->compressorRing)) != nullptr) {
		orderAndSendEventPackets(state, packetContainer);
	}
}

static void orderAndSendEventPackets(outputCommonState state, caerEventPacketContainer currPacketContainer) {
	// Sort container by first timestamp (required) and by type ID (convenience).
	size_t currPacketContainerSize = (size_t) caerEventPacketContainerGetEventPacketsNumber(currPacketContainer);

	qsort(currPacketContainer->eventPackets, currPacketContainerSize, sizeof(caerEventPacketHeader),
		&packetsFirstTimestampThenTypeCmp);

	for (size_t cpIdx = 0; cpIdx < currPacketContainerSize; cpIdx++) {
		// Send the packets out to the file descriptor.
		sendEventPacket(state, caerEventPacketContainerGetEventPacket(currPacketContainer, (int32_t) cpIdx));
	}

	// Free packet container. The individual packets have already been either
	// freed on error, or have been transferred out.
	free(currPacketContainer);
}

static int packetsFirstTimestampThenTypeCmp(const void *a, const void *b) {
	const caerEventPacketHeader *aa = (const caerEventPacketHeader *) a;
	const caerEventPacketHeader *bb = (const caerEventPacketHeader *) b;

	// Sort first by timestamp of the first event.
	int32_t eventTimestampA = caerGenericEventGetTimestamp(caerGenericEventGetEvent(*aa, 0), *aa);
	int32_t eventTimestampB = caerGenericEventGetTimestamp(caerGenericEventGetEvent(*bb, 0), *bb);

	if (eventTimestampA < eventTimestampB) {
		return (-1);
	}
	else if (eventTimestampA > eventTimestampB) {
		return (1);
	}
	else {
		// If equal, further sort by type ID.
		int16_t eventTypeA = caerEventPacketHeaderGetEventType(*aa);
		int16_t eventTypeB = caerEventPacketHeaderGetEventType(*bb);

		if (eventTypeA < eventTypeB) {
			return (-1);
		}
		else if (eventTypeA > eventTypeB) {
			return (1);
		}
		else {
			return (0);
		}
	}
}

static void sendEventPacket(outputCommonState state, caerEventPacketHeader packet) {
	// Calculate total size of packet, in bytes.
	size_t packetSize = CAER_EVENT_PACKET_HEADER_SIZE
		+ (size_t) (caerEventPacketHeaderGetEventNumber(packet) * caerEventPacketHeaderGetEventSize(packet));

	// Statistics support.
	state->statistics.packetsNumber++;
	state->statistics.packetsTotalSize += packetSize;
	state->statistics.packetsHeaderSize += CAER_EVENT_PACKET_HEADER_SIZE;
	state->statistics.packetsDataSize += (size_t) (caerEventPacketHeaderGetEventNumber(packet)
		* caerEventPacketHeaderGetEventSize(packet));

	if (state->formatID != 0) {
		packetSize = compressEventPacket(state, packet, packetSize);
	}

	// Statistics support (after compression).
	state->statistics.dataWritten += packetSize;

	// Send compressed packet out to output handling thread.
	// Already format it as a libuv buffer.
	try {
		auto packetBuffer = new asio::const_buffer(packet, packetSize);

		// Put packet buffer onto output ring-buffer. Retry until successful.
		while (!caerRingBufferPut(state->outputRing, packetBuffer)) {
			// If the output thread failed, we'd forever block here, if it can't accept
			// any more data. So we detect that condition and discard remaining packets.
			if (state->outputThreadFailure.load(std::memory_order_relaxed)) {
				break;
			}

			// Delay by 500 µs if no change, to avoid a wasteful busy loop.
			std::this_thread::sleep_for(std::chrono::microseconds(500));
		}
	}
	catch (const std::bad_alloc &) {
		free(packet);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for packet buffer.");
		return;
	}
}

/**
 * Compress event packets.
 * Compressed event packets have the highest bit of the type field
 * set to '1' (type | 0x8000). Their eventCapacity field holds the
 * new, true length of the data portion of the packet, in bytes.
 * This takes advantage of the fact capacity always equals number
 * in any input/output stream, and as such is redundant information.
 *
 * @param state common output state.
 * @param packet the event packet to compress.
 * @param packetSize the current event packet size (header + data).
 *
 * @return the event packet size (header + data) after compression.
 *         Must be equal or smaller than the input packetSize.
 */
static size_t compressEventPacket(outputCommonState state, caerEventPacketHeader packet, size_t packetSize) {
	size_t compressedSize = packetSize;

	// Data compression technique 1: serialize timestamps for event types that tend to repeat them a lot.
	// Currently, this means polarity events.
	if ((state->formatID & 0x01) && caerEventPacketHeaderGetEventType(packet) == POLARITY_EVENT) {
		compressedSize = compressTimestampSerialize(state, packet);
	}

#ifdef ENABLE_INOUT_PNG_COMPRESSION
	// Data compression technique 2: do PNG compression on frames, Grayscale and RGB(A).
	if ((state->formatID & 0x02) && caerEventPacketHeaderGetEventType(packet) == FRAME_EVENT) {
		compressedSize = compressFramePNG(state, packet);
	}
#endif

	// If any compression was possible, we mark the packet as compressed
	// and store its data size in eventCapacity.
	if (compressedSize != packetSize) {
		packet->eventType = htole16(le16toh(packet->eventType) | I16T(0x8000));
		packet->eventCapacity = htole32(I32T(compressedSize) - CAER_EVENT_PACKET_HEADER_SIZE);
	}

	// Return size after compression.
	return (compressedSize);
}

/**
 * Search for runs of at least 3 events with the same timestamp, and convert them to a special
 * sequence: leave first event unchanged, but mark its timestamp as special by setting the
 * highest bit (bit 31) to one (it is forbidden for timestamps in memory to have that bit set for
 * signed-integer-only language compatibility). Then, for the second event, change its timestamp
 * to a 4-byte integer saying how many more events will follow afterwards with this same timestamp
 * (this is used for decoding), so only their data portion will be given. Then follow with those
 * event's data, back to back, with their timestamps removed.
 * So let's assume there are 6 events with TS=1234. In memory this looks like this:
 * E1(data,ts), E2(data,ts), E3(data,ts), E4(data,ts), E5(data,ts), E6(data,ts)
 * After the timestamp serialization compression step:
 * E1(data,ts|0x80000000), E2(data,4), E3(data), E4(data), E5(data), E5(data)
 * This change is only in the data itself, not in the packet headers, so that we can still use the
 * eventNumber and eventSize fields to calculate memory allocation when doing decompression.
 * As such, to correctly interpret this data, the Format flags must be correctly set. All current
 * file or network formats do specify those as mandatory in their headers, so we can rely on that.
 * Also all event types where this kind of thing makes any sense do have the timestamp as their last
 * data member in their struct, so we can use that information, stored in tsOffset header field,
 * together with eventSize, to come up with a generic implementation applicable to all other event
 * types that satisfy this condition of TS-as-last-member (so we can use that offset as event size).
 * When this is enabled, it requires full iteration thorough the whole event packet, both at
 * compression and at decompression time.
 *
 * @param state common output state.
 * @param packet the packet to timestamp-compress.
 *
 * @return the event packet size (header + data) after compression.
 *         Must be equal or smaller than the input packetSize.
 */
static size_t compressTimestampSerialize(outputCommonState state, caerEventPacketHeader packet) {
	UNUSED_ARGUMENT(state);

	size_t currPacketOffset = CAER_EVENT_PACKET_HEADER_SIZE; // Start here, no change to header.
	int32_t eventSize = caerEventPacketHeaderGetEventSize(packet);
	int32_t eventTSOffset = caerEventPacketHeaderGetEventTSOffset(packet);

	int32_t lastTS = -1;
	int32_t currTS = -1;
	size_t tsRun = 0;
	bool doMemMove = false; // Initially don't move memory, until we actually shrink the size.

	for (int32_t caerIteratorCounter = 0; caerIteratorCounter <= caerEventPacketHeaderGetEventNumber(packet);
		caerIteratorCounter++) {
		// Iterate until one element past the end, to flush the last run. In that particular case,
		// we don't get a new element or TS, as we'd be past the end of the array.
		if (caerIteratorCounter < caerEventPacketHeaderGetEventNumber(packet)) {
			const void *caerIteratorElement = caerGenericEventGetEvent(packet, caerIteratorCounter);

			currTS = caerGenericEventGetTimestamp(caerIteratorElement, packet);
			if (currTS == lastTS) {
				// Increase size of run of same TS events currently being seen.
				tsRun++;
				continue;
			}
		}

		// TS are different, at this point look if the last run was long enough
		// and if it makes sense to compress. It does starting with 3 events.
		if (tsRun >= 3) {
			// First event to remains there, we set its TS highest bit.
			const uint8_t *firstEvent = (const uint8_t *) caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun--);
			caerGenericEventSetTimestamp((void *) firstEvent, packet,
				caerGenericEventGetTimestamp(firstEvent, packet) | I32T(0x80000000));

			// Now use second event's timestamp for storing how many further events.
			const uint8_t *secondEvent = (const uint8_t *) caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun--);
			caerGenericEventSetTimestamp((void *) secondEvent, packet, I32T(tsRun)); // Is at least 1.

			// Finally move modified memory where it needs to go.
			if (doMemMove) {
				memmove(((uint8_t *) packet) + currPacketOffset, firstEvent, (size_t) eventSize * 2);
			}
			else {
				doMemMove = true; // After first shrink always move memory.
			}
			currPacketOffset += (size_t) eventSize * 2;

			// Now go through remaining events and move their data close together.
			while (tsRun > 0) {
				const uint8_t *thirdEvent = (const uint8_t *) caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun--);
				memmove(((uint8_t *) packet) + currPacketOffset, thirdEvent, (size_t) eventTSOffset);
				currPacketOffset += (size_t) eventTSOffset;
			}
		}
		else {
			// Just copy data unchanged if no compression is possible.
			if (doMemMove) {
				const uint8_t *startEvent = (const uint8_t *) caerGenericEventGetEvent(packet, caerIteratorCounter - (int32_t) tsRun);
				memmove(((uint8_t *) packet) + currPacketOffset, startEvent, (size_t) eventSize * tsRun);
			}
			currPacketOffset += (size_t) eventSize * tsRun;
		}

		// Reset values for next iteration.
		lastTS = currTS;
		tsRun = 1;
	}

	return (currPacketOffset);
}

#ifdef ENABLE_INOUT_PNG_COMPRESSION

// Simple structure to store PNG image bytes.
struct caer_libpng_buffer {
	uint8_t *buffer;
	size_t size;
};

static void caerLibPNGWriteBuffer(png_structp png_ptr, png_bytep data, png_size_t length) {
	struct caer_libpng_buffer *p = (struct caer_libpng_buffer *) png_get_io_ptr(png_ptr);
	size_t newSize = p->size + length;
	uint8_t *bufferSave = p->buffer;

	// Allocate or grow buffer as needed.
	if (p->buffer) {
		p->buffer = (uint8_t *) realloc(p->buffer, newSize);
	}
	else {
		p->buffer = (uint8_t *) malloc(newSize);
	}

	if (p->buffer == nullptr) {
		free(bufferSave); // Free on realloc() failure.
		png_error(png_ptr, "Write Buffer Error");
	}

	// Copy the new bytes to the end of the buffer.
	memcpy(p->buffer + p->size, data, length);
	p->size += length;
}

static inline int caerFrameEventColorToLibPNG(enum caer_frame_event_color_channels channels) {
	switch (channels) {
		case GRAYSCALE:
			return (PNG_COLOR_TYPE_GRAY);
			break;

		case RGB:
			return (PNG_COLOR_TYPE_RGB);
			break;

		case RGBA:
		default:
			return (PNG_COLOR_TYPE_RGBA);
			break;
	}
}

static inline bool caerFrameEventPNGCompress(uint8_t **outBuffer, size_t *outSize, uint16_t *inBuffer, int32_t xSize,
	int32_t ySize, enum caer_frame_event_color_channels channels) {
	png_structp png_ptr = nullptr;
	png_infop info_ptr = nullptr;
	png_bytepp row_pointers = nullptr;

	// Initialize the write struct.
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (png_ptr == nullptr) {
		return (false);
	}

	// Initialize the info struct.
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr) {
		png_destroy_write_struct(&png_ptr, nullptr);
		return (false);
	}

	// Set up error handling.
	if (setjmp(png_jmpbuf(png_ptr))) {
		if (row_pointers != nullptr) {
			png_free(png_ptr, row_pointers);
		}
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return (false);
	}

	// Set image attributes.
	png_set_IHDR(png_ptr, info_ptr, (png_uint_32) xSize, (png_uint_32) ySize, 16, caerFrameEventColorToLibPNG(channels),
	PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	// Handle endianness of 16-bit depth pixels correctly.
	// PNG assumes big-endian, our Frame Event is always little-endian.
	png_set_swap(png_ptr);

	// Initialize rows of PNG.
	row_pointers = (png_bytepp) png_malloc(png_ptr, (size_t) ySize * sizeof(png_bytep));
	if (row_pointers == nullptr) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return (false);
	}

	for (size_t y = 0; y < (size_t) ySize; y++) {
		row_pointers[y] = (png_bytep) &inBuffer[y * (size_t) xSize * channels];
	}

	// Set write function to buffer one.
	struct caer_libpng_buffer state = { .buffer = nullptr, .size = 0 };
	png_set_write_fn(png_ptr, &state, &caerLibPNGWriteBuffer, nullptr);

	// Actually write the image data.
	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

	// Free allocated memory for rows.
	png_free(png_ptr, row_pointers);

	// Destroy main structs.
	png_destroy_write_struct(&png_ptr, &info_ptr);

	// Pass out buffer with resulting PNG image.
	*outBuffer = state.buffer;
	*outSize = state.size;

	return (true);
}

static size_t compressFramePNG(outputCommonState state, caerEventPacketHeader packet) {
	size_t currPacketOffset = CAER_EVENT_PACKET_HEADER_SIZE; // Start here, no change to header.
	// '- sizeof(uint16_t)' to compensate for pixels[1] at end of struct for C++ compatibility.
	size_t frameEventHeaderSize = (sizeof(struct caer_frame_event) - sizeof(uint16_t));

	CAER_FRAME_ITERATOR_ALL_START((caerFrameEventPacket) packet)
		size_t pixelSize = caerFrameEventGetPixelsSize(caerFrameIteratorElement);

		uint8_t *outBuffer;
		size_t outSize;
		if (!caerFrameEventPNGCompress(&outBuffer, &outSize,
			caerFrameEventGetPixelArrayUnsafe(caerFrameIteratorElement),
			caerFrameEventGetLengthX(caerFrameIteratorElement), caerFrameEventGetLengthY(caerFrameIteratorElement),
			caerFrameEventGetChannelNumber(caerFrameIteratorElement))) {
			// Failed to generate PNG.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to compress frame event. "
				"PNG generation from frame failed. Keeping uncompressed frame.");

			// Copy this frame uncompressed. Don't want to loose data.
			size_t fullCopySize = frameEventHeaderSize + pixelSize;
			memmove(((uint8_t *) packet) + currPacketOffset, caerFrameIteratorElement, fullCopySize);
			currPacketOffset += fullCopySize;

			continue;
		}

		// Add integer needed for storing PNG block length.
		size_t pngSize = outSize + sizeof(int32_t);

		// Check that the image didn't actually grow or fail to compress.
		// If we don't gain any size advantages, just keep it uncompressed.
		if (pngSize >= pixelSize) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to compress frame event. "
				"Image didn't shrink, original: %zu, compressed: %zu, difference: %zu.", pixelSize, pngSize,
				(pngSize - pixelSize));

			// Copy this frame uncompressed. Don't want to loose data.
			size_t fullCopySize = frameEventHeaderSize + pixelSize;
			memmove(((uint8_t *) packet) + currPacketOffset, caerFrameIteratorElement, fullCopySize);
			currPacketOffset += fullCopySize;

			free(outBuffer);
			continue;
		}

		// Mark frame as PNG compressed. Use info member in frame event header struct,
		// to store highest bit equals one.
		SET_NUMBITS32(caerFrameIteratorElement->info, 31, 0x01, 1);

		// Keep frame event header intact, copy all image data, move memory close together.
		memmove(((uint8_t *) packet) + currPacketOffset, caerFrameIteratorElement, frameEventHeaderSize);
		currPacketOffset += frameEventHeaderSize;

		// Store size of PNG image block as 4 byte integer.
		*((int32_t *) (((uint8_t *) packet) + currPacketOffset)) = htole32(I32T(outSize));
		currPacketOffset += sizeof(int32_t);

		memcpy(((uint8_t *) packet) + currPacketOffset, outBuffer, outSize);
		currPacketOffset += outSize;

		// Free allocated PNG block memory.
		free(outBuffer);
	}

	return (currPacketOffset);
}

#endif

/**
 * ============================================================================
 * OUTPUT THREAD
 * ============================================================================
 * Handle writing of data to output.
 * ============================================================================
 */
bool setupOutputThread(outputCommonState state, void (*headerInitFunc)(outputCommonState state));
struct aedat3_network_header initializeNetworkHeader(outputCommonState state);
void writeFileHeader(outputCommonState state);

static inline void errorCleanup(outputCommonState state, asio::const_buffer *packetBuffer) {
	// Free currently held memory.
	if (packetBuffer != nullptr) {
		free(boost::asio::buffer_cast<caerEventPacketHeader>(*packetBuffer));
		delete packetBuffer;
	}

	// Signal failure to compressor thread.
	state->outputThreadFailure.store(true);

	// Ensure parent also shuts down on unrecoverable failures, taking the
	// compressor thread with it.
	sshsNodePutBool(state->parentModule->moduleNode, "running", false);
}

bool setupOutputThread(outputCommonState state, void (*headerInitFunc)(outputCommonState state)) {
	// Set thread name.
	size_t threadNameLength = strlen(state->parentModule->moduleSubSystemString);
	char threadName[threadNameLength + 1 + 8]; // +1 for NUL character.
	strcpy(threadName, state->parentModule->moduleSubSystemString);
	strcat(threadName, "[Output]");
	portable_thread_set_name(threadName);

	bool headerSent = false;

	while (state->running.load(std::memory_order_relaxed)) {
		// Wait for source to be defined.
		int16_t sourceID = state->sourceID.load(std::memory_order_relaxed);
		if (sourceID == -1) {
			// Delay by 1 ms if no data, to avoid a wasteful busy loop.
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		// Send appropriate header.
		(*headerInitFunc)(state);

		headerSent = true;
		break;
	}

	// If no header sent, it means we exited (running=false) without ever getting any
	// event packet with a source ID, so we don't have to process anything.
	// But we make sure to empty the transfer ring-buffer, as something may have been
	// put there in the meantime, so we ensure it's checked and freed. This because
	// in caerOutputCommonExit() we expect the ring-buffer to always be empty!
	if (!headerSent) {
		asio::const_buffer *packetBuffer;
		while ((packetBuffer = (asio::const_buffer *) caerRingBufferGet(state->outputRing)) != nullptr) {
			free(boost::asio::buffer_cast<caerEventPacketHeader>(*packetBuffer));
			delete packetBuffer;
		}

		return (false);
	}

	return (true);
}

struct aedat3_network_header initializeNetworkHeader(outputCommonState state) {
	struct aedat3_network_header networkHeader;

	// Generate AEDAT 3.1 header for network streams (20 bytes total).
	networkHeader.magicNumber = htole64(AEDAT3_NETWORK_MAGIC_NUMBER);
	networkHeader.sequenceNumber = htole64(0);
	networkHeader.versionNumber = AEDAT3_NETWORK_VERSION;
	networkHeader.formatNumber = state->formatID; // Send numeric format ID.
	networkHeader.sourceID = htole16(I16T(atomic_load(&state->sourceID))); // Always one source per output module.

	return (networkHeader);
}

asio::const_buffer *generateNetworkHeader(struct aedat3_network_header &networkHeader, bool isUDP, bool startOfUDPPacket) {
	// Create memory chunk for network header to be sent via libuv.
	// libuv takes care of freeing memory. This is also needed for UDP
	// to have different sequence numbers in flight.
	auto networkHeaderBuffer = new uint8_t[AEDAT3_NETWORK_HEADER_LENGTH];

	if (isUDP && startOfUDPPacket) {
		// Set highest bit of sequence number to one.
		networkHeader.sequenceNumber = htole64(
			I64T(le64toh(networkHeader.sequenceNumber) | I64T(0x8000000000000000LL)));
	}

	// Copy in current header.
	memcpy(networkHeaderBuffer, &networkHeader, AEDAT3_NETWORK_HEADER_LENGTH);

	if (isUDP) {
		if (startOfUDPPacket) {
			// Unset highest bit of sequence number (back to zero).
			networkHeader.sequenceNumber = htole64(
				I64T(le64toh(networkHeader.sequenceNumber) & I64T(0x7FFFFFFFFFFFFFFFLL)));
		}

		// Increase sequence number for successive headers, if this is a
		// message-based network protocol (UDP for example).
		networkHeader.sequenceNumber = htole64(I64T(le64toh(networkHeader.sequenceNumber) + 1));
	}

	auto networkHeaderASIO = new asio::const_buffer(networkHeaderBuffer, AEDAT3_NETWORK_HEADER_LENGTH);

	return (networkHeaderASIO);
}

static void writeFileHeader(outputCommonState state, std::fstream &file) {
	// Write AEDAT 3.1 header.
	file.write("#!AER-DAT" AEDAT3_FILE_VERSION "\r\n", 11 + strlen(AEDAT3_FILE_VERSION));

	// Write format header for all supported formats.
	file.write("#Format: ", 9);

	if (state->formatID == 0x00) {
		file.write("RAW", 3);
	}
	else {
		// Support the various formats and their mixing.
		if (state->formatID == 0x01) {
			file.write("SerializedTS", 12);
		}

		if (state->formatID == 0x02) {
			file.write("PNGFrames", 9);
		}

		if (state->formatID == 0x03) {
			// Serial and PNG together.
			file.write("SerializedTS,PNGFrames", 12 + 1 + 9);
		}
	}

	file.write("\r\n", 2);

	file.write(state->sourceInfoString.c_str(), state->sourceInfoString.length());

	// First prepend the time.
	time_t currentTimeEpoch = time(nullptr);

#if defined(OS_WINDOWS)
	// localtime() is thread-safe on Windows (and there is no localtime_r() at all).
	struct tm *currentTime = localtime(&currentTimeEpoch);

	// Windows doesn't support %z (numerical timezone), so no TZ info here.
	// Following time format uses exactly 34 characters (20 separators/characters,
	// 4 year, 2 month, 2 day, 2 hours, 2 minutes, 2 seconds).
	const size_t currentTimeStringLength = 34;
	char currentTimeString[currentTimeStringLength + 1];// + 1 for terminating NUL byte.
	strftime(currentTimeString, currentTimeStringLength + 1, "#Start-Time: %Y-%m-%d %H:%M:%S\r\n", currentTime);
#else
	// From localtime_r() man-page: "According to POSIX.1-2004, localtime()
	// is required to behave as though tzset(3) was called, while
	// localtime_r() does not have this requirement."
	// So we make sure to call it here, to be portable.
	tzset();

	struct tm currentTime;
	localtime_r(&currentTimeEpoch, &currentTime);

	// Following time format uses exactly 44 characters (25 separators/characters,
	// 4 year, 2 month, 2 day, 2 hours, 2 minutes, 2 seconds, 5 time-zone).
	const size_t currentTimeStringLength = 44;
	char currentTimeString[currentTimeStringLength + 1]; // + 1 for terminating NUL byte.
	strftime(currentTimeString, currentTimeStringLength + 1, "#Start-Time: %Y-%m-%d %H:%M:%S (TZ%z)\r\n", &currentTime);
#endif

	file.write(currentTimeString, currentTimeStringLength);

	file.write("#!END-HEADER\r\n", 14);
}

// Net server: check max num connections
// Network: send network header, track client (conn/IP)

bool caerOutputCommonInit(caerModuleData moduleData) {
	outputCommonState state = moduleData->moduleState;

	state->parentModule = moduleData;

	// If in server mode, add SSHS attribute to track connected client IPs.
	if (state->isNetworkStream && state->networkIO->server != nullptr) {
		sshsNodeCreateString(state->parentModule->moduleNode, "connectedClients", "", 0, INT32_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "IPs of clients currently connected to output server.");
	}

	// Initial source ID has to be -1 (invalid).
	state->sourceID.store(-1);

	// Handle configuration.
	sshsNodeCreateBool(moduleData->moduleNode, "validOnly", false, SSHS_FLAGS_NORMAL, "Only send valid events.");
	sshsNodeCreateBool(moduleData->moduleNode, "keepPackets", false, SSHS_FLAGS_NORMAL,
		"Ensure all packets are kept (stall output if transfer-buffer full).");
	sshsNodeCreateInt(moduleData->moduleNode, "ringBufferSize", 512, 8, 4096, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer and EventPacket queues, used for transfers between mainloop and output threads.");

	state->validOnly.store(sshsNodeGetBool(moduleData->moduleNode, "validOnly"));
	state->keepPackets.store(sshsNodeGetBool(moduleData->moduleNode, "keepPackets"));
	int ringSize = sshsNodeGetInt(moduleData->moduleNode, "ringBufferSize");

	// Format configuration (compression modes).
	state->formatID = 0x00; // RAW format by default.

	// Initialize compressor ring-buffer. ringBufferSize only changes here at init time!
	state->compressorRing = caerRingBufferInit((size_t) ringSize);
	if (state->compressorRing == nullptr) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate compressor ring-buffer.");
		return (false);
	}

	// Initialize output ring-buffer. ringBufferSize only changes here at init time!
	state->outputRing = caerRingBufferInit((size_t) ringSize);
	if (state->outputRing == nullptr) {
		caerRingBufferFree(state->compressorRing);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate output ring-buffer.");
		return (false);
	}

	// If network output, initialize common libuv components.
	if (state->isNetworkStream) {
		// Add support for asynchronous shutdown (from caerOutputCommonExit()).
		state->networkIO->shutdown.data = state;
		int retVal = uv_async_init(&state->networkIO->loop, &state->networkIO->shutdown, &libuvAsyncShutdown);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_async_init",
			caerRingBufferFree(state->compressorRing); caerRingBufferFree(state->outputRing); return (false));

		// Use idle handles to check for new data on every loop run.
		state->networkIO->ringBufferGet.data = state;
		retVal = uv_idle_init(&state->networkIO->loop, &state->networkIO->ringBufferGet);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_idle_init",
			uv_close((uv_handle_t *) &state->networkIO->shutdown, nullptr); caerRingBufferFree(state->compressorRing); caerRingBufferFree(state->outputRing); return (false));

		retVal = uv_idle_start(&state->networkIO->ringBufferGet, &libuvRingBufferGet);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_idle_start",
			uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, nullptr); uv_close((uv_handle_t *) &state->networkIO->shutdown, nullptr); caerRingBufferFree(state->compressorRing); caerRingBufferFree(state->outputRing); return (false));
	}

	// Start output handling thread.
	state->running.store(true);

	if (thrd_create(&state->compressorThread, &compressorThread, state) != thrd_success) {
		if (state->isNetworkStream) {
			uv_idle_stop(&state->networkIO->ringBufferGet);
			uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, nullptr);
			uv_close((uv_handle_t *) &state->networkIO->shutdown, nullptr);
		}
		caerRingBufferFree(state->compressorRing);
		caerRingBufferFree(state->outputRing);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start compressor thread.");
		return (false);
	}

	if (thrd_create(&state->outputThread, &outputThread, state) != thrd_success) {
		// Stop compressor thread (started just above) and wait on it.
		state->running.store(false);

		if ((errno = thrd_join(state->compressorThread, nullptr)) != thrd_success) {
			// This should never happen!
			caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to join compressor thread. Error: %d.",
			errno);
		}

		if (state->isNetworkStream) {
			uv_idle_stop(&state->networkIO->ringBufferGet);
			uv_close((uv_handle_t *) &state->networkIO->ringBufferGet, nullptr);
			uv_close((uv_handle_t *) &state->networkIO->shutdown, nullptr);
		}
		caerRingBufferFree(state->compressorRing);
		caerRingBufferFree(state->outputRing);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start output thread.");
		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerOutputCommonConfigListener);

	return (true);
}

void caerOutputCommonExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerOutputCommonConfigListener);

	outputCommonState state = moduleData->moduleState;

	// Stop output thread and wait on it.
	state->running.store(false);
	if (state->isNetworkStream) {
		uv_async_send(&state->networkIO->shutdown);
	}

	if ((errno = thrd_join(state->compressorThread, nullptr)) != thrd_success) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to join compressor thread. Error: %d.", errno);
	}

	if ((errno = thrd_join(state->outputThread, nullptr)) != thrd_success) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to join output thread. Error: %d.", errno);
	}

	// Now clean up the ring-buffers: they should be empty, so sanity check!
	caerEventPacketContainer packetContainer;

	while ((packetContainer = caerRingBufferGet(state->compressorRing)) != nullptr) {
		caerEventPacketContainerFree(packetContainer);

		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Compressor ring-buffer was not empty!");
	}

	caerRingBufferFree(state->compressorRing);

	libuvWriteBuf packetBuffer;

	while ((packetBuffer = caerRingBufferGet(state->outputRing)) != nullptr) {
		free(packetBuffer);

		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Output ring-buffer was not empty!");
	}

	caerRingBufferFree(state->outputRing);

	// Cleanup IO resources.
	if (state->isNetworkStream) {
		if (state->networkIO->server != nullptr) {
			// Server shut down, no more clients.
			sshsNodeRemoveAttribute(state->parentModule->moduleNode, "connectedClients", SSHS_STRING);
		}

		// Cleanup all remaining handles and run until all callbacks are done.
		int retVal = libuvCloseLoopHandles(&state->networkIO->loop);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "libuvCloseLoopHandles",);

		retVal = uv_loop_close(&state->networkIO->loop);
		UV_RET_CHECK(retVal, state->parentModule->moduleSubSystemString, "uv_loop_close",);

		// Free allocated memory. libuv already frees all client/server related memory.
		free(state->networkIO->address);
		free(state->networkIO);
	}
	else {
		// Ensure all data written to disk.
		portable_fsync(state->fileIO);

		// Close file descriptor.
		close(state->fileIO);
	}

	free(state->sourceInfoString);

	// Print final statistics results.
	caerModuleLog(state->parentModule, CAER_LOG_INFO,
		"Statistics: wrote %" PRIu64 " packets, for a total uncompressed size of %" PRIu64 " bytes (%" PRIu64 " bytes header + %" PRIu64 " bytes data). "
		"Actually written to output were %" PRIu64 " bytes (after compression), resulting in a saving of %" PRIu64 " bytes.",
		state->statistics.packetsNumber, state->statistics.packetsTotalSize, state->statistics.packetsHeaderSize,
		state->statistics.packetsDataSize, state->statistics.dataWritten,
		(state->statistics.packetsTotalSize - state->statistics.dataWritten));
}

static void caerOutputCommonConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;
	outputCommonState state = moduleData->moduleState;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "validOnly")) {
			// Set valid only flag to given value.
			state->validOnly.store(changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "keepPackets")) {
			// Set keep packets flag to given value.
			state->keepPackets.store(changeValue.boolean);
		}
	}
}
