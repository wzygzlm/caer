#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"

#include <libcaer/events/polarity.h>

struct BAFilter_state {
	simple2DBufferLong timestampMap;
	int32_t deltaT;
	int8_t subSampleBy;
	int64_t invalidPointNum;
};

typedef struct BAFilter_state *BAFilterState;

static bool caerBackgroundActivityFilterInit(caerModuleData moduleData);
static void caerBackgroundActivityFilterRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerBackgroundActivityFilterConfig(caerModuleData moduleData);
static void caerBackgroundActivityFilterExit(caerModuleData moduleData);
static void caerBackgroundActivityFilterReset(caerModuleData moduleData, uint16_t resetCallSourceID);
static bool allocateTimestampMap(BAFilterState state, int16_t sourceID);

static const struct caer_module_functions BAFilterFunctions = { .moduleInit = &caerBackgroundActivityFilterInit,
	.moduleRun = &caerBackgroundActivityFilterRun, .moduleConfig = &caerBackgroundActivityFilterConfig, .moduleExit =
		&caerBackgroundActivityFilterExit, .moduleReset = &caerBackgroundActivityFilterReset };

static const struct caer_event_stream_in BAFilterInputs[] = { { .type = POLARITY_EVENT, .number = 1 } };
static const struct caer_event_stream_out BAFilterOutputs[] = { { .type = POLARITY_EVENT, .number = 1 } };

static const struct caer_module_info BAFilterInfo = { .version = 1, .name = "BAFilter", .type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct BAFilter_state), .functions = &BAFilterFunctions, .inputStreams = BAFilterInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(BAFilterInputs), .outputStreams = BAFilterOutputs, .outputStreamsSize =
		CAER_EVENT_STREAM_OUT_SIZE(BAFilterOutputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&BAFilterInfo);
}

static bool caerBackgroundActivityFilterInit(caerModuleData moduleData) {
	sshsNodeCreateInt(moduleData->moduleNode, "deltaT", 30000, 1, 10000000, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(moduleData->moduleNode, "subSampleBy", 0, 0, 20, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(moduleData->moduleNode, "testingMode", false, SSHS_FLAGS_NORMAL);

	// Always initialize to zero at init.
	// Corresponding variable is already zero in state memory.
	sshsNodePutLong(moduleData->moduleNode, "invalidPointNum", 0);

	BAFilterState state = moduleData->moduleState;

	state->deltaT = sshsNodeGetInt(moduleData->moduleNode, "deltaT");
	state->subSampleBy = sshsNodeGetByte(moduleData->moduleNode, "subSampleBy");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerBackgroundActivityFilterRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	caerPolarityEventPacket polarity = (caerPolarityEventPacket) caerEventPacketContainerFindEventPacketByType(in,
		POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	BAFilterState state = moduleData->moduleState;

	// If the map is not allocated yet, do it.
	if (state->timestampMap == NULL) {
		if (!allocateTimestampMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for timestampMap.");
			return;
		}
	}

	// Iterate over events and filter out ones that are not supported by other
	// events within a certain region in the specified timeframe.
	CAER_POLARITY_ITERATOR_VALID_START(polarity)
		// Get values on which to operate.
		int64_t ts = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

		// Apply sub-sampling.
		x = U16T(x >> state->subSampleBy);
		y = U16T(y >> state->subSampleBy);

		// Get value from map.
		int64_t lastTS = state->timestampMap->buffer2d[x][y];

		if ((I64T(ts - lastTS) >= I64T(state->deltaT)) || (lastTS == 0)) {
			// Filter out invalid.
			caerPolarityEventInvalidate(caerPolarityIteratorElement, polarity);
			state->invalidPointNum++;
		}

		// Update neighboring region.
		size_t sizeMaxX = (state->timestampMap->sizeX - 1);
		size_t sizeMaxY = (state->timestampMap->sizeY - 1);

		if (x > 0) {
			state->timestampMap->buffer2d[x - 1][y] = ts;
		}
		if (x < sizeMaxX) {
			state->timestampMap->buffer2d[x + 1][y] = ts;
		}

		if (y > 0) {
			state->timestampMap->buffer2d[x][y - 1] = ts;
		}
		if (y < sizeMaxY) {
			state->timestampMap->buffer2d[x][y + 1] = ts;
		}

		if (x > 0 && y > 0) {
			state->timestampMap->buffer2d[x - 1][y - 1] = ts;
		}
		if (x < sizeMaxX && y < sizeMaxY) {
			state->timestampMap->buffer2d[x + 1][y + 1] = ts;
		}

		if (x > 0 && y < sizeMaxY) {
			state->timestampMap->buffer2d[x - 1][y + 1] = ts;
		}
		if (x < sizeMaxX && y > 0) {
			state->timestampMap->buffer2d[x + 1][y - 1] = ts;
		}
	CAER_POLARITY_ITERATOR_VALID_END

	// Only update SSHS once per packet (expensive call).
	sshsNodePutLong(moduleData->moduleNode, "invalidPointNum", state->invalidPointNum);
}

static void caerBackgroundActivityFilterConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	BAFilterState state = moduleData->moduleState;

	state->deltaT = sshsNodeGetInt(moduleData->moduleNode, "deltaT");
	state->subSampleBy = sshsNodeGetByte(moduleData->moduleNode, "subSampleBy");
}

static void caerBackgroundActivityFilterExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	BAFilterState state = moduleData->moduleState;

	// Ensure map is freed.
	simple2DBufferFreeLong(state->timestampMap);
}

static void caerBackgroundActivityFilterReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	BAFilterState state = moduleData->moduleState;

	// Reset timestamp map to all zeros (startup state).
	simple2DBufferResetLong(state->timestampMap);
}

static bool allocateTimestampMap(BAFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate timestamp map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	state->timestampMap = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->timestampMap == NULL) {
		return (false);
	}

	// TODO: size the map differently if subSampleBy is set!
	return (true);
}
