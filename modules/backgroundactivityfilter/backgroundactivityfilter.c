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
static void caerBackgroundActivityFilterReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions BAFilterFunctions = { .moduleInit = &caerBackgroundActivityFilterInit,
	.moduleRun = &caerBackgroundActivityFilterRun, .moduleConfig = &caerBackgroundActivityFilterConfig, .moduleExit =
		&caerBackgroundActivityFilterExit, .moduleReset = &caerBackgroundActivityFilterReset };

static const struct caer_event_stream_in BAFilterInputs[] =
	{ { .type = POLARITY_EVENT, .number = 1, .readOnly = false } };

static const struct caer_module_config BAFilterConfig[] = { { SSHS_INT, "/", "deltaT", SSHS_VALUE_INT(30000),
	SSHS_RANGES_LONG(1, 10000000), SSHS_FLAGS_NORMAL,
	"Maximum time difference in µs for events to be considered correlated and not be filtered out." }, { SSHS_BYTE, "/",
	"subSampleBy", SSHS_VALUE_BYTE(0), SSHS_RANGES_LONG(0, 20), SSHS_FLAGS_NORMAL,
	"Sub-sample event addresses by shifting right by this amount." }, };

static const struct caer_module_info BAFilterInfo = { .version = 1, .name = "BAFilter", .description =
	"Filters background noise events.", .type = CAER_MODULE_PROCESSOR, .memSize = sizeof(struct BAFilter_state),
	.functions = &BAFilterFunctions, .configuration = BAFilterConfig, .configurationSize = CAER_MODULE_CONFIG_SIZE(
		BAFilterConfig), .inputStreams = BAFilterInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(BAFilterInputs),
	.outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&BAFilterInfo);
}

static bool caerBackgroundActivityFilterInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	// Always initialize to zero at init.
	// Corresponding variable is already zero in state memory.
	sshsNodeCreateLong(moduleData->moduleNode, "invalidPointNum", 0, 0, INT64_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of events filtered out by this module.");

	BAFilterState state = moduleData->moduleState;

	// Allocate map using info from sourceInfo.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfo, "polaritySizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfo, "polaritySizeY");

	state->timestampMap = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->timestampMap == NULL) {
		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for timestampMap.");
		return (false);
	}

	caerBackgroundActivityFilterConfig(moduleData);

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
		}CAER_POLARITY_ITERATOR_VALID_END

	// Only update SSHS once per packet (expensive call).
	sshsNodeUpdateReadOnlyAttribute(moduleData->moduleNode, "invalidPointNum", SSHS_LONG,
		(union sshs_node_attr_value ) { .ilong = state->invalidPointNum });
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

	// Remove informative attribute.
	sshsNodeRemoveAttribute(moduleData->moduleNode, "invalidPointNum", SSHS_LONG);

	BAFilterState state = moduleData->moduleState;

	// Ensure map is freed.
	simple2DBufferFreeLong(state->timestampMap);
}

static void caerBackgroundActivityFilterReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	BAFilterState state = moduleData->moduleState;

	// Reset timestamp map to all zeros (startup state).
	simple2DBufferResetLong(state->timestampMap);

	// Reset invalid points counters to zero (startup state).
	state->invalidPointNum = 0;
	sshsNodeUpdateReadOnlyAttribute(moduleData->moduleNode, "invalidPointNum", SSHS_LONG,
		(union sshs_node_attr_value ) { .ilong = 0 });
}
