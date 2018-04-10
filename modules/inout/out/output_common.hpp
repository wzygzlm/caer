#ifndef OUTPUT_COMMON_HPP_
#define OUTPUT_COMMON_HPP_

#include "caer-sdk/mainloop.h"
#include "../inout_common.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <regex>
#include <string>

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include <libcaer/ringbuffer.h>

#include <libcaercpp/libcaer.hpp>
using namespace libcaer::log;

namespace asio = boost::asio;

#define MAX_OUTPUT_RINGBUFFER_GET 10
#define MAX_OUTPUT_QUEUED_SIZE (1 * 1024 * 1024) // 1MB outstanding writes

struct output_common_statistics {
	uint64_t packetsNumber;
	uint64_t packetsTotalSize;
	uint64_t packetsHeaderSize;
	uint64_t packetsDataSize;
	uint64_t dataWritten;
};

struct output_common_state {
	/// Control flag for output handling thread.
	std::atomic<bool> running;
	/// The compression handling thread (separate as to not hold up processing).
	std::thread compressorThread;
	/// The output handling thread (separate as to not hold up processing).
	std::thread outputThread;
	/// Detect unrecoverable failure of output thread. Used so that the compressor
	/// thread can break out of trying to send data to the output thread, if that
	/// one is incapable of accepting it.
	std::atomic<bool> outputThreadFailure;
	/// Track source ID (cannot change!). One source per I/O module!
	std::atomic<int16_t> sourceID;
	/// Source information string for that particular source ID.
	/// Must be set by mainloop, external threads cannot get it directly!
	std::string sourceInfoString;
	/// Filter out invalidated events or not.
	std::atomic<bool> validOnly;
	/// Force all incoming packets to be committed to the transfer ring-buffer.
	/// This results in no loss of data, but may slow down processing considerably.
	/// It may also block it altogether, if the output goes away for any reason.
	std::atomic<bool> keepPackets;
	/// Transfer packets coming from a mainloop run to the compression handling thread.
	/// We use EventPacketContainers as data structure for convenience, they do exactly
	/// keep track of the data we do want to transfer and are part of libcaer.
	caerRingBuffer compressorRing;
	/// Transfer buffers to output handling thread.
	caerRingBuffer outputRing;
	/// Track last packet container's highest event timestamp that was sent out.
	int64_t lastTimestamp;
	/// Support different formats, providing data compression.
	int8_t formatID;
	/// Output module statistics collection.
	struct output_common_statistics statistics;
	/// Reference to parent module's original data.
	caerModuleData parentModule;
};

typedef struct output_common_state *outputCommonState;

bool caerOutputCommonInit(caerModuleData moduleData);
void caerOutputCommonExit(caerModuleData moduleData);
void caerOutputCommonRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
void caerOutputCommonReset(caerModuleData moduleData, int16_t resetCallSourceID);


#endif /* OUTPUT_COMMON_HPP_ */
