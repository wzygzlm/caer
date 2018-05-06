#include "caer-sdk/mainloop.h"

#include <libcaer/events/polarity.h>
#include <libcaer/filters/dvs_noise.h>

static void caerDVSNoiseFilterConfigInit(sshsNode moduleNode);
static bool caerDVSNoiseFilterInit(caerModuleData moduleData);
static void caerDVSNoiseFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerDVSNoiseFilterConfig(caerModuleData moduleData);
static void caerDVSNoiseFilterExit(caerModuleData moduleData);
static void caerDVSNoiseFilterReset(caerModuleData moduleData, int16_t resetCallSourceID);

static void statisticsPassthrough(void *userData, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value *value);

static const struct caer_module_functions DVSNoiseFilterFunctions = { .moduleConfigInit = &caerDVSNoiseFilterConfigInit,
	.moduleInit = &caerDVSNoiseFilterInit, .moduleRun = &caerDVSNoiseFilterRun, .moduleConfig =
		&caerDVSNoiseFilterConfig, .moduleExit = &caerDVSNoiseFilterExit, .moduleReset = &caerDVSNoiseFilterReset };

static const struct caer_event_stream_in DVSNoiseFilterInputs[] = { { .type = POLARITY_EVENT, .number = 1, .readOnly =
false } };

static const struct caer_module_info DVSNoiseFilterInfo = { .version = 1, .name = "DVSNoiseFilter", .description =
	"Filters out DVS noise events.", .type = CAER_MODULE_PROCESSOR, .memSize = 0, .functions = &DVSNoiseFilterFunctions,
	.inputStreams = DVSNoiseFilterInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(DVSNoiseFilterInputs),
	.outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DVSNoiseFilterInfo);
}

static void caerDVSNoiseFilterConfigInit(sshsNode moduleNode) {
	sshsNodeCreateBool(moduleNode, "backgroundActivityEnable", true, SSHS_FLAGS_NORMAL,
		"Enable the background activity filter.");
	sshsNodeCreateInt(moduleNode, "backgroundActivityTime", 20000, 0, 10000000, SSHS_FLAGS_NORMAL,
		"Maximum time difference in µs for events to be considered correlated and not be filtered out.");
	sshsNodeCreateLong(moduleNode, "backgroundActivityFiltered", 0, 0, INT64_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Number of events filtered out by the background activity filter.");
	sshsNodeCreateAttributePollTime(moduleNode, "backgroundActivityFiltered", SSHS_LONG, 2);

	sshsNodeCreateBool(moduleNode, "refractoryPeriodEnable", true, SSHS_FLAGS_NORMAL,
		"Enable the refractory period filter.");
	sshsNodeCreateInt(moduleNode, "refractoryPeriodTime", 100, 0, 10000000, SSHS_FLAGS_NORMAL,
		"Minimum time between events to not be filtered out.");
	sshsNodeCreateLong(moduleNode, "refractoryPeriodFiltered", 0, 0, INT64_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of events filtered out by the refractory period filter.");
	sshsNodeCreateAttributePollTime(moduleNode, "refractoryPeriodFiltered", SSHS_LONG, 2);
}

static void statisticsPassthrough(void *userData, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value *value) {
	UNUSED_ARGUMENT(type); // We know all statistics are always LONG.

	caerFilterDVSNoise state = userData;

	uint64_t statisticValue = 0;

	if (caerStrEquals(key, "backgroundActivityFiltered")) {
		caerFilterDVSNoiseConfigGet(state, CAER_FILTER_DVS_BACKGROUND_ACTIVITY_STATISTICS, &statisticValue);
	}
	else if (caerStrEquals(key, "refractoryPeriodFiltered")) {
		caerFilterDVSNoiseConfigGet(state, CAER_FILTER_DVS_REFRACTORY_PERIOD_STATISTICS, &statisticValue);
	}

	value->ilong = I64T(statisticValue);
}

static bool caerDVSNoiseFilterInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	// Allocate map using info from sourceInfo.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfo, "polaritySizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfo, "polaritySizeY");

	moduleData->moduleState = caerFilterDVSNoiseInitialize(U16T(sizeX), U16T(sizeY));
	if (moduleData->moduleState == NULL) {
		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to initialize DVS Noise filter.");
		return (false);
	}

	caerDVSNoiseFilterConfig(moduleData);

	// Add read passthrough modifiers, they need access to moduleState.
	sshsNodeAddAttributeReadModifier(moduleData->moduleNode, "backgroundActivityFiltered", SSHS_LONG,
		moduleData->moduleState, &statisticsPassthrough);
	sshsNodeAddAttributeReadModifier(moduleData->moduleNode, "refractoryPeriodFiltered", SSHS_LONG,
		moduleData->moduleState, &statisticsPassthrough);

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerDVSNoiseFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	caerPolarityEventPacket polarity = (caerPolarityEventPacket) caerEventPacketContainerFindEventPacketByType(in,
		POLARITY_EVENT);

	caerFilterDVSNoiseApply(moduleData->moduleState, polarity);
}

static void caerDVSNoiseFilterConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	caerFilterDVSNoise state = moduleData->moduleState;

	caerFilterDVSNoiseConfigSet(state, CAER_FILTER_DVS_BACKGROUND_ACTIVITY_ENABLE,
		sshsNodeGetBool(moduleData->moduleNode, "backgroundActivityEnable"));
	caerFilterDVSNoiseConfigSet(state, CAER_FILTER_DVS_BACKGROUND_ACTIVITY_TIME,
		U32T(sshsNodeGetInt(moduleData->moduleNode, "backgroundActivityTime")));

	caerFilterDVSNoiseConfigSet(state, CAER_FILTER_DVS_REFRACTORY_PERIOD_ENABLE,
		sshsNodeGetBool(moduleData->moduleNode, "refractoryPeriodEnable"));
	caerFilterDVSNoiseConfigSet(state, CAER_FILTER_DVS_REFRACTORY_PERIOD_TIME,
		U32T(sshsNodeGetInt(moduleData->moduleNode, "refractoryPeriodTime")));
}

static void caerDVSNoiseFilterExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	sshsNodeRemoveAllAttributeReadModifiers(moduleData->moduleNode);

	caerFilterDVSNoiseDestroy(moduleData->moduleState);
}

static void caerDVSNoiseFilterReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	// TODO: how to handle changes in size (sourceInfo content) in source modules,
	// without a init/destroy cycle? Document/solve this!
}
