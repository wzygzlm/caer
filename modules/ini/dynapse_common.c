#include "dynapse_common.h"
#include "ext/buffers.h"
#include "ext/colorjet/colorjet.h"
#include <unistd.h>
#include <limits.h>

static uint32_t convertBias(const char *biasName, const char* lowhi, const char*cl, const char*sex, uint8_t enal,
	uint16_t fineValue, uint8_t coarseValue, uint8_t special);
static uint32_t generateCoarseFineBias(sshsNode biasNode);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void spikeConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void usbConfigSend(sshsNode node, caerModuleData moduleData);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void updateLowPowerBiases(caerModuleData moduleData, int chipid);
static void updateSilentBiases(caerModuleData moduleData, int chipid);
static bool EnableStimuliGen(caerModuleData moduleData);
static bool DisableStimuliGen(caerModuleData moduleData);
static bool setCamContent(caerInputDynapseState state, int16_t chipId, bool ei, bool fs, int16_t address,
	int8_t source_core, int8_t coreId, int16_t row, int16_t column);

bool caerInputDYNAPSEInit(caerModuleData moduleData);
void caerInputDYNAPSEExit(caerModuleData moduleData);
void caerInputDYNAPSERun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static bool caerInputDYNAPSEFX2Init(caerModuleData moduleData);

static const struct caer_module_functions DynapseFX2Functions = { .moduleInit = &caerInputDYNAPSEFX2Init, .moduleRun =
	&caerInputDYNAPSERun, .moduleConfig = NULL, .moduleExit = &caerInputDYNAPSEExit };

static const struct caer_event_stream_out DynapseFX2Outputs[] = { { .type = SPECIAL_EVENT }, { .type = SPIKE_EVENT } };

static const struct caer_module_info DynapseFX2Info = { .version = 1, .name = "DynapseFX2", .description =
	"Connects to a Dynap-se neuromorphic processor to get data.", .type = CAER_MODULE_INPUT, .memSize =
	sizeof(struct caer_input_dynapse_state), .functions = &DynapseFX2Functions, .inputStreams = NULL,
	.inputStreamsSize = 0, .outputStreams = DynapseFX2Outputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(
		DynapseFX2Outputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DynapseFX2Info);
}

static bool caerInputDYNAPSEFX2Init(caerModuleData moduleData) {
	return (caerInputDYNAPSEInit(moduleData));
}

bool EnableStimuliGen(caerModuleData moduleData) {
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));
	sshsNode spikeNode = sshsGetRelativeNode(deviceConfigNode, "spikeGen/");

	sshsNodePutBool(spikeNode, "doStimPrimitiveBias", true);
	return (true);
}

bool DisableStimuliGen(caerModuleData moduleData) {

	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));
	sshsNode spikeNode = sshsGetRelativeNode(deviceConfigNode, "spikeGen/");

	sshsNodePutBool(spikeNode, "doStimPrimitiveBias", false);
	return (true);
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void chipConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferNumber")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState, CAER_HOST_CONFIG_USB,
			CAER_HOST_CONFIG_USB_BUFFER_NUMBER, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferSize")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState, CAER_HOST_CONFIG_USB,
			CAER_HOST_CONFIG_USB_BUFFER_SIZE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EarlyPacketDelay")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState, DYNAPSE_CONFIG_USB,
			DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY, U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState, DYNAPSE_CONFIG_USB,
			DYNAPSE_CONFIG_USB_RUN, changeValue.boolean);
		}
	}
}

static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerMaxPacketSize")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			CAER_HOST_CONFIG_PACKETS,
			CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerInterval")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			CAER_HOST_CONFIG_PACKETS,
			CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(changeValue.iint));
		}
	}
}

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
	CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER, U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
	CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE, U32T(sshsNodeGetInt(node, "BufferSize")));

	caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
	DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY, U32T(sshsNodeGetShort(node, "EarlyPacketDelay")));
	caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
	DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_RUN, sshsNodeGetBool(node, "Run"));
}

static void spikeConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	//caerModuleData moduleData = userData;
	caerInputDynapseState state = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStim")) { // && caerStrEquals(changeKey, "doStimBias")
		//atomic_load(&state->genSpikeState.doStim);
			if (changeValue.boolean) {
				//caerModuleLog(CAER_LOG_NOTICE, "spikeGen", "stimulation started.");
				atomic_store(&state->genSpikeState.done, false); // we just started
				atomic_store(&state->genSpikeState.started, true);
			}
			else {
				//caerModuleLog(CAER_LOG_NOTICE, "spikeGen", "stimulation ended.");
				atomic_store(&state->genSpikeState.started, false);
				atomic_store(&state->genSpikeState.done, true);
			}
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_type")) {
			atomic_store(&state->genSpikeState.stim_type, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_avr")) {
			atomic_store(&state->genSpikeState.stim_avr, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_std")) {
			atomic_store(&state->genSpikeState.stim_std, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_duration")) {
			atomic_store(&state->genSpikeState.stim_duration, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "repeat")) {
			atomic_store(&state->genSpikeState.repeat, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "teaching")) {
			atomic_store(&state->genSpikeState.teaching, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sendTeachingStimuli")) {
			atomic_store(&state->genSpikeState.sendTeachingStimuli, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sendInhibitoryStimuli")) {
			atomic_store(&state->genSpikeState.sendInhibitoryStimuli, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "setCam")) {
			atomic_store(&state->genSpikeState.setCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "setCamSingle")) {
			atomic_store(&state->genSpikeState.setCamSingle, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "clearCam")) {
			atomic_store(&state->genSpikeState.clearCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "clearAllCam")) {
			atomic_store(&state->genSpikeState.clearAllCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStimPrimitiveBias")) {
			atomic_store(&state->genSpikeState.doStimPrimitiveBias, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStimPrimitiveCam")) {
			atomic_store(&state->genSpikeState.doStimPrimitiveCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "loadDefaultBiases")) {
			atomic_store(&state->genSpikeState.loadDefaultBiases, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
			atomic_store(&state->genSpikeState.running, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sx")) {
			atomic_store(&state->genSpikeState.sx, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sy")) {
			atomic_store(&state->genSpikeState.sy, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "dx")) {
			atomic_store(&state->genSpikeState.dx, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "dy")) {
			atomic_store(&state->genSpikeState.dy, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "core_d")) {
			atomic_store(&state->genSpikeState.core_d, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "core_s")) {
			atomic_store(&state->genSpikeState.core_s, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "address")) {
			atomic_store(&state->genSpikeState.address, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "chip_id")) {
			atomic_store(&state->genSpikeState.chip_id, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFphase_num")) {
			atomic_store(&state->genSpikeState.ETFphase_num, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFstarted")) {
			atomic_store(&state->genSpikeState.ETFstarted, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFdone")) {
			atomic_store(&state->genSpikeState.ETFdone, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFchip_id")) {
			atomic_store(&state->genSpikeState.ETFchip_id, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFduration")) {
			atomic_store(&state->genSpikeState.ETFduration, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFphase_num")) {
			atomic_store(&state->genSpikeState.ETFphase_num, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFrepeat")) {
			atomic_store(&state->genSpikeState.ETFrepeat, changeValue.boolean);
		}
	}

}

static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferNumber")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferSize")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EarlyPacketDelay")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY, U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_RUN, changeValue.boolean);
		}
	}
}

static void updateCoarseFineBiasSetting(caerModuleData moduleData, const char *biasName, uint8_t coarseValue,
	uint16_t fineValue, const char *hlbias, const char *currentLevel, const char *sex, bool enabled, int chipid) {

	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNodeLP = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName((int16_t) chipid, true));

	sshsNode biasNodeLP = sshsGetRelativeNode(deviceConfigNodeLP, "bias/");

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNodeLP, biasNameFull);

	// Add bias settings.
	sshsNodePutByte(biasConfigNode, "coarseValue", I8T(coarseValue));
	sshsNodePutShort(biasConfigNode, "fineValue", I16T(fineValue));
	sshsNodePutString(biasConfigNode, "BiasLowHi", hlbias);
	sshsNodePutString(biasConfigNode, "currentLevel", currentLevel);
	sshsNodePutString(biasConfigNode, "sex", sex);
	sshsNodePutBool(biasConfigNode, "enabled", enabled);
	sshsNodePutBool(biasConfigNode, "special", false);

	uint32_t value = generateCoarseFineBias(biasConfigNode);
	// finally send configuration via USB
	int retval = caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
	DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, value);

	if (retval == false) {
		caerModuleLog(moduleData, CAER_LOG_CRITICAL, "failed to set bias");
	}
}

static void createCoarseFineBiasSetting(sshsNode biasNode, caerModuleData moduleData, const char *biasName, uint8_t coarseValue,
	uint16_t fineValue, const char *hlbias, const char *currentLevel, const char *sex, bool enabled) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateByte(biasConfigNode, "coarseValue", I8T(coarseValue), 0, 7, SSHS_FLAGS_NORMAL,
		"Coarse current value (big adjustments).");
	sshsNodeCreateShort(biasConfigNode, "fineValue", I16T(fineValue), 0, 255, SSHS_FLAGS_NORMAL,
		"Fine current value (small adjustments).");
	sshsNodeCreateBool(biasConfigNode, "enabled", enabled, SSHS_FLAGS_NORMAL, "Bias enabled.");
	sshsNodeCreateString(biasConfigNode, "sex", sex, 5, 5, SSHS_FLAGS_NORMAL, "Bias sex.");
	sshsNodeCreateString(biasConfigNode, "currentLevel", currentLevel, 3, 7, SSHS_FLAGS_NORMAL, "Bias current level.");
	sshsNodeCreateString(biasConfigNode, "BiasLowHi", hlbias, 0, 100, SSHS_FLAGS_NORMAL, "Bias low or high TODO.");
	sshsNodeCreateBool(biasConfigNode, "special", false, SSHS_FLAGS_NORMAL, "Bias special type TODO.");

	sshsNodeAddAttributeListener(biasConfigNode, moduleData, &biasConfigListener);

}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {

		sshsNode parent = sshsNodeGetParent(node);
		sshsNode grandparent = sshsNodeGetParent(parent);
		const char *nodeGrandParent = sshsNodeGetName(grandparent);
		uint32_t value = generateCoarseFineBias(node);

		if (caerStrEquals(nodeGrandParent, "DYNAPSE_CONFIG_DYNAPSE_U0")) {
			int retval = caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U0);
			if (retval == false) {
				caerModuleLog(moduleData, CAER_LOG_CRITICAL,
					"failed to set DYNAPSE_CONFIG_CHIP_ID to DYNAPSE_CONFIG_DYNAPSE_U0");
			}
		}
		else if (caerStrEquals(nodeGrandParent, "DYNAPSE_CONFIG_DYNAPSE_U1")) {
			int retval = caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U1);
			if (retval == false) {
				caerModuleLog(moduleData, CAER_LOG_CRITICAL,
					"failed to set DYNAPSE_CONFIG_CHIP_ID to DYNAPSE_CONFIG_DYNAPSE_U1");
			}
		}
		else if (caerStrEquals(nodeGrandParent, "DYNAPSE_CONFIG_DYNAPSE_U2")) {
			int retval = caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U2);
			if (retval == false) {
				caerModuleLog(moduleData, CAER_LOG_CRITICAL,
					"failed to set DYNAPSE_CONFIG_CHIP_ID to DYNAPSE_CONFIG_DYNAPSE_U2");
			}
		}
		else if (caerStrEquals(nodeGrandParent, "DYNAPSE_CONFIG_DYNAPSE_U3")) {
			int retval = caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
			DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U3);
			if (retval == false) {
				caerModuleLog(moduleData, CAER_LOG_CRITICAL,
					"failed to set DYNAPSE_CONFIG_CHIP_ID to DYNAPSE_CONFIG_DYNAPSE_U3");
			}
		}

		// finally send configuration via USB
		int retval = caerDeviceConfigSet(((caerInputDynapseState) moduleData->moduleState)->deviceState,
		DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, value);

		if (retval == false) {
			caerModuleLog(moduleData, CAER_LOG_CRITICAL, "failed to set bias");
		}
	}
}

static void updateLowPowerBiases(caerModuleData moduleData, int chipid) {

	// now set default low power biases
	updateCoarseFineBiasSetting(moduleData, "C0_IF_BUF_P", 3, 80, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_RFR_N", 3, 3, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_DC_P", 1, 30, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_TAU1_N", 7, 10, "LowBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_TAU2_N", 6, 100, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_THR_N", 3, 120, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_AHTAU_N", 7, 35, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PULSE_PWLK_P", 3, 106, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_EXC_F_N", 15, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_TAU_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_THR_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_THR_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_TAU_F_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_TAU_F_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_R2R_P", 4, 85, "HighBias", "Normal", "PBias", true, chipid);

	// core 1
	updateCoarseFineBiasSetting(moduleData, "C1_IF_BUF_P", 3, 80, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_RFR_N", 3, 3, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_DC_P", 1, 30, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_TAU1_N", 7, 5, "LowBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_TAU2_N", 6, 100, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_THR_N", 4, 120, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_AHTAU_N", 7, 35, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PULSE_PWLK_P", 3, 106, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_EXC_F_N", 15, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_TAU_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_THR_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_TAU_F_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_TAU_F_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_R2R_P", 4, 85, "HighBias", "Normal", "PBias", true, chipid);

	// core 2
	updateCoarseFineBiasSetting(moduleData, "C2_IF_BUF_P", 3, 80, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_RFR_N", 3, 3, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_DC_P", 1, 30, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_TAU1_N", 7, 10, "LowBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_TAU2_N", 6, 100, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_THR_N", 4, 120, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_AHTAU_N", 7, 35, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PULSE_PWLK_P", 3, 106, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_EXC_F_N", 15, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_TAU_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_THR_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_TAU_F_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_R2R_P", 4, 85, "HighBias", "Normal", "PBias", true, chipid);

	// core 3
	updateCoarseFineBiasSetting(moduleData, "C3_IF_BUF_P", 3, 80, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_RFR_N", 3, 3, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_DC_P", 1, 30, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_TAU1_N", 7, 5, "LowBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_TAU2_N", 6, 100, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_THR_N", 4, 120, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_AHTAU_N", 7, 35, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PULSE_PWLK_P", 3, 106, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_TAU_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_THR_S_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_TAU_F_P", 7, 40, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_R2R_P", 4, 85, "HighBias", "Normal", "PBias", true, chipid);

	updateCoarseFineBiasSetting(moduleData, "D_BUFFER", 1, 2, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "D_SSP", 0, 7, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "D_SSN", 0, 15, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "U_BUFFER", 1, 2, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "U_SSP", 0, 7, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "U_SSN", 0, 15, "HighBias", "Normal", "PBias", true, chipid);

}

static void updateSilentBiases(caerModuleData moduleData, int chipid) {

	// make chip silent while programming AER
	// core 0
	updateCoarseFineBiasSetting(moduleData, "C0_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_TAU1_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_TAU2_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C0_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);

	// core 1
	updateCoarseFineBiasSetting(moduleData, "C1_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_TAU1_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_TAU2_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C1_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);

	// core 2
	updateCoarseFineBiasSetting(moduleData, "C2_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_TAU1_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_TAU2_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C2_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);

	// core 3
	updateCoarseFineBiasSetting(moduleData, "C3_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_TAU1_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_TAU2_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "C3_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true, chipid);

	updateCoarseFineBiasSetting(moduleData, "D_BUFFER", 1, 2, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "D_SSP", 0, 7, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "D_SSN", 0, 15, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "U_BUFFER", 1, 2, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "U_SSP", 0, 7, "HighBias", "Normal", "PBias", true, chipid);
	updateCoarseFineBiasSetting(moduleData, "U_SSN", 0, 15, "HighBias", "Normal", "PBias", true, chipid);

}

static void createDefaultConfiguration(caerModuleData moduleData, int chipid) {

	// Device related configuration has its own sub-node..
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName((int16_t) chipid, true));

	// Chip biases, defaults.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_BUF_P", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_TAU1_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_TAU2_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPII_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C0_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true);

	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_TAU1_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_TAU2_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPII_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C1_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true);

	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_TAU1_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_TAU2_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPII_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C2_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true);

	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_BUF_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_RFR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_NMDA_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_DC_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_TAU1_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_TAU2_N", 7, 0, "LowBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_THR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_AHW_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_AHTAU_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_AHTHR_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_IF_CASC_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_PULSE_PWLK_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_PS_WEIGHT_EXC_F_N", 7, 0, "HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPII_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPII_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPII_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPII_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPIE_TAU_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPIE_THR_S_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPIE_TAU_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_NPDPIE_THR_F_P", 7, 0, "HighBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "C3_R2R_P", 7, 0, "HighBias", "Normal", "PBias", true);

	createCoarseFineBiasSetting(biasNode, moduleData, "U_BUFFER", 1, 80, "LowBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "U_SSP", 0, 7, "LowBias", "Cascade", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "U_SSN", 0, 15, "LowBias", "Normal", "PBias", true);

	createCoarseFineBiasSetting(biasNode, moduleData, "D_BUFFER", 1, 80, "LowBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "D_SSP", 0, 7, "LowBias", "Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, moduleData, "D_SSN", 0, 15, "LowBias", "Normal", "PBias", true);

}

static uint32_t convertBias(const char *biasName, const char* lowhi, const char*cl, const char*sex, uint8_t enal,
	uint16_t fineValue, uint8_t coarseValue, uint8_t special) {

	int32_t confbits;
	int32_t addr = 0;
	uint32_t inbits = 0;

	/*start names*/
	if (caerStrEquals(biasName, "C0_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C0_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C0_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C0_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C0_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C0_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C0_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C0_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C0_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C0_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C0_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C0_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C0_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_R2R_P;
	}

	if (caerStrEquals(biasName, "C1_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C1_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C1_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C1_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C1_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C1_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C1_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C1_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C1_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C1_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C1_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C1_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C1_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_R2R_P;
	}

	if (caerStrEquals(biasName, "C2_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C2_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C2_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C2_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C2_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C2_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C2_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C2_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C2_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C2_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C2_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C2_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C2_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_R2R_P;
	}

	if (caerStrEquals(biasName, "C3_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C3_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C3_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C3_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C3_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C3_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C3_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C3_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C3_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C3_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C3_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C3_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C3_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_R2R_P;
	}

	if (caerStrEquals(biasName, "U_BUFFER")) {
		addr = DYNAPSE_CONFIG_BIAS_U_BUFFER;
	}
	if (caerStrEquals(biasName, "U_SSP")) {
		addr = DYNAPSE_CONFIG_BIAS_U_SSP;
	}
	if (caerStrEquals(biasName, "U_SSN")) {
		addr = DYNAPSE_CONFIG_BIAS_U_SSN;
	}
	if (caerStrEquals(biasName, "D_BUFFER")) {
		addr = DYNAPSE_CONFIG_BIAS_D_BUFFER;
	}
	if (caerStrEquals(biasName, "D_SSP")) {
		addr = DYNAPSE_CONFIG_BIAS_D_SSP;
	}
	if (caerStrEquals(biasName, "D_SSN")) {
		addr = DYNAPSE_CONFIG_BIAS_D_SSN;
	}

	uint8_t lws, ssx, cls;
	if (caerStrEquals(lowhi, "HighBias")) {
		//"HighBias": 1,
		lws = 1;
	}
	else {
		//"LowBias": 0,
		lws = 0;
	}
	if (caerStrEquals(sex, "NBias")) {
		//"NBias": 1,
		ssx = 1;
	}
	else {
		//  "PBias": 0,
		ssx = 0;
	}
	if (caerStrEquals(cl, "Normal")) {
		//"Normal": 1,
		cls = 1;
	}
	else {
		//"CascodeBias": 0,
		cls = 0;
	}

	caerLog(CAER_LOG_DEBUG, "BIAS CONFIGURE ", " biasName %s --> ADDR %d coarseValue %d", biasName, addr, coarseValue);

	/*end names*/
	if (enal == 1) {
		// "BiasEnable": 1,
		confbits = lws << 3 | cls << 2 | ssx << 1 | 1;
	}
	else {
		// "BiasDisable": 0,
		confbits = lws << 3 | cls << 2 | ssx << 1;
	}

	uint8_t coarseRev = 0;
	/*reverse*/

	/*same as: sum(1 << (2 - i) for i in range(3) if 2 >> i & 1)*/
	if (coarseValue == 0)
		coarseValue = 0;
	else if (coarseValue == 1)
		coarseValue = 4;
	else if (coarseValue == 2)
		coarseValue = 2;
	else if (coarseValue == 3)
		coarseValue = 6;
	else if (coarseValue == 4)
		coarseValue = 1;
	else if (coarseValue == 5)
		coarseValue = 5;
	else if (coarseValue == 6)
		coarseValue = 3;
	else if (coarseValue == 7)
		coarseValue = 7;

	coarseRev = coarseValue;

	// snn and ssp
	if (addr == DYNAPSE_CONFIG_BIAS_U_SSP || addr == DYNAPSE_CONFIG_BIAS_U_SSN || addr == DYNAPSE_CONFIG_BIAS_D_SSP
		|| addr == DYNAPSE_CONFIG_BIAS_D_SSN) {
		confbits = 0;
		inbits = (uint32_t) addr << 18 | 1 << 16 | 63 << 10 | (uint32_t) fineValue << 4 | (uint32_t) confbits;
	}
	else if (addr == DYNAPSE_CONFIG_BIAS_D_BUFFER || addr == DYNAPSE_CONFIG_BIAS_U_BUFFER) {
		confbits = 0;
		inbits = (uint32_t) addr << 18 | 1 << 16 | (uint32_t) special << 15 | (uint32_t) coarseRev << 12
			| (uint32_t) fineValue << 4;
	}
	else {
		inbits = (uint32_t) addr << 18 | 1 << 16 | (uint32_t) special << 15 | (uint32_t) coarseRev << 12
			| (uint32_t) fineValue << 4 | (uint32_t) confbits;
	}

	return (inbits);

}

uint32_t generateCoarseFineBias(sshsNode biasNode) {

	const char *biasName = sshsNodeGetName(biasNode);

	bool enal = sshsNodeGetBool(biasNode, "enabled");
	bool special = sshsNodeGetBool(biasNode, "special");
	int8_t coarseValue = sshsNodeGetByte(biasNode, "coarseValue");
	int16_t fineValue = sshsNodeGetShort(biasNode, "fineValue");
	char * lowhi = sshsNodeGetString(biasNode, "BiasLowHi");
	char * cl = sshsNodeGetString(biasNode, "currentLevel");
	char * sex = sshsNodeGetString(biasNode, "sex");

	// generates bits values
	uint8_t enabled;
	uint8_t specialed;
	if (enal)
		enabled = 1;
	else
		enabled = 0;
	if (special)
		specialed = 1;
	else
		specialed = 0;

	uint32_t bits = convertBias(biasName, lowhi, cl, sex, enal, (uint16_t) fineValue, (uint8_t) coarseValue, specialed);

	return (bits);
}

static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_dynapse_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// Send cAER configuration to libcaer and device.
	usbConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "usb/"), moduleData);
}

bool caerInputDYNAPSEInit(caerModuleData moduleData) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// USB port/bus/SN settings/restrictions.
	// These can be used to force connection to one specific device at startup.
	sshsNodeCreateShort(moduleData->moduleNode, "busNumber", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL,
		"USB bus number restriction.");
	sshsNodeCreateShort(moduleData->moduleNode, "devAddress", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL,
		"USB device address restriction.");
	sshsNodeCreateString(moduleData->moduleNode, "serialNumber", "", 0, 8, SSHS_FLAGS_NORMAL,
		"USB serial number restriction.");

	// Add auto-restart setting.
	sshsNodeCreateBool(moduleData->moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL,
		"Automatically restart module after shutdown.");

	//Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	char *serialNumber = sshsNodeGetString(moduleData->moduleNode, "serialNumber");

	caerInputDynapseState state = moduleData->moduleState;

	state->deviceState = caerDeviceOpen(moduleData->moduleID, CAER_DEVICE_DYNAPSE, 0, 0, NULL);
	state->eventSourceConfigNode = moduleData->moduleNode;

	free(serialNumber);

	if (state->deviceState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Let's take a look at the information we have on the device.
	struct caer_dynapse_info dynapse_info = caerDynapseInfoGet(state->deviceState);

	caerModuleLog(moduleData, CAER_LOG_NOTICE, "%s --- ID: %d, Master: %d,  Logic: %d,  ChipID: %d.",
		dynapse_info.deviceString, dynapse_info.deviceID, dynapse_info.deviceIsMaster, dynapse_info.logicVersion,
		dynapse_info.chipID);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateLong(sourceInfoNode, "highestTimestamp", -1, -1, INT64_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Highest timestamp generated by device.");

	sshsNodeCreateShort(sourceInfoNode, "logicVersion", dynapse_info.logicVersion, dynapse_info.logicVersion,
		dynapse_info.logicVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device FPGA logic version.");
	sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", dynapse_info.deviceIsMaster,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Timestamp synchronization support: device master status.");
	sshsNodeCreateShort(sourceInfoNode, "chipID", dynapse_info.chipID, dynapse_info.chipID, dynapse_info.chipID,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device chip identification number.");

	// Put source information for generic visualization, to be used to display and debug filter information.
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": %s\r", moduleData->moduleID,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U2, false));

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": %s\r", moduleData->moduleID,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U2, false));
	sourceString[sourceStringLength] = '\0';

	sshsNodeCreateString(sourceInfoNode, "sourceString", sourceString, sourceStringLength, sourceStringLength,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device source information.");

	// Generate sub-system string for module.
	size_t subSystemStringLength = (size_t) snprintf(NULL, 0, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, dynapse_info.deviceSerialNumber, dynapse_info.deviceUSBBusNumber,
		dynapse_info.deviceUSBDeviceAddress);

	char subSystemString[subSystemStringLength + 1];
	snprintf(subSystemString, subSystemStringLength + 1, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, dynapse_info.deviceSerialNumber, dynapse_info.deviceUSBBusNumber,
		dynapse_info.deviceUSBDeviceAddress);
	subSystemString[subSystemStringLength] = '\0';

	caerModuleSetSubSystemString(moduleData, subSystemString);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(state->deviceState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, false);

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_REQ_DELAY, 30);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_REQ_EXTENSION, 30);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_RUN, false);

	// Device related configuration has its own sub-node DYNAPSEFX2
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));

	// create default configuration FX2 USB Configuration and USB buffer settings.
	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeCreateBool(usbNode, "Run", true, SSHS_FLAGS_NORMAL,
		"Enable the USB state machine (FPGA to USB data exchange).");
	sshsNodeCreateInt(usbNode, "BufferNumber", 8, 2, 128, SSHS_FLAGS_NORMAL, "Number of USB transfers.");
	sshsNodeCreateInt(usbNode, "BufferSize", 4096, 512, 32768, SSHS_FLAGS_NORMAL,
		"Size in bytes of data buffers for USB transfers.");
	sshsNodeCreateShort(usbNode, "EarlyPacketDelay", 8, 1, 8000, SSHS_FLAGS_NORMAL,
		"Send early USB packets if this timeout is reached (in 125µs time-slices).");


	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");

	// Packet settings (size (in events) and time interval (in µs)).
	sshsNodeCreateInt(sysNode, "PacketContainerMaxPacketSize", 4096, 1, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Maximum packet size in events, when any packet reaches this size, the EventPacketContainer is sent for processing.");
	sshsNodeCreateInt(sysNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL,
		"Time interval in µs, each sent EventPacketContainer will span this interval.");

	// Ring-buffer setting (only changes value on module init/shutdown cycles).
	sshsNodeCreateInt(sysNode, "DataExchangeBufferSize", 64, 8, 1024, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer queue, used for transfers between data acquisition thread and mainloop.");

	// send default usb configuration
	sendDefaultConfiguration(moduleData, &dynapse_info);

	// Create default settings and send them to the devices.
	createDefaultConfiguration(moduleData, DYNAPSE_CONFIG_DYNAPSE_U0);
	createDefaultConfiguration(moduleData, DYNAPSE_CONFIG_DYNAPSE_U1);
	createDefaultConfiguration(moduleData, DYNAPSE_CONFIG_DYNAPSE_U2);
	createDefaultConfiguration(moduleData, DYNAPSE_CONFIG_DYNAPSE_U3);

	// Update silent biases
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	updateSilentBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	updateSilentBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	updateSilentBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
	updateSilentBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U3);

	// Clear SRAM --> DYNAPSE_CONFIG_DYNAPSE_U0
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Clearing SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, DYNAPSE_CONFIG_DYNAPSE_U0, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// Clear SRAM --> DYNAPSE_CONFIG_DYNAPSE_U1
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Clearing SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, DYNAPSE_CONFIG_DYNAPSE_U1, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// Clear SRAM --> DYNAPSE_CONFIG_DYNAPSE_U2
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Clearing SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// Clear SRAM --> DYNAPSE_CONFIG_DYNAPSE_U3
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Clearing SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, DYNAPSE_CONFIG_DYNAPSE_U3, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	//  DYNAPSE_CONFIG_DYNAPSE_U0
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	updateLowPowerBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U0);
	//  DYNAPSE_CONFIG_DYNAPSE_U1
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	updateLowPowerBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U1);
	// DYNAPSE_CONFIG_DYNAPSE_U2
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	updateLowPowerBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U2);
	// DYNAPSE_CONFIG_DYNAPSE_U3
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
	updateLowPowerBiases(moduleData, DYNAPSE_CONFIG_DYNAPSE_U3);

	// Configure SRAM for Monitoring--> DYNAPSE_CONFIG_DYNAPSE_U0
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Configuring Default SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U0, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// Configure SRAM for Monitoring--> DYNAPSE_CONFIG_DYNAPSE_U1
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Configuring Default SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U1, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// Configure SRAM for Monitoring--> DYNAPSE_CONFIG_DYNAPSE_U2
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Configuring Default SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// Configure SRAM for Monitoring--> DYNAPSE_CONFIG_DYNAPSE_U3
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Configuring Default SRAM ...");
	caerModuleLog(moduleData, CAER_LOG_NOTICE, "Device number  %d...", DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U3, 0);
	caerModuleLog(moduleData, CAER_LOG_NOTICE, " Done.");

	// chip node
	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	// config listeners
	sshsNodeAddAttributeListener(chipNode, moduleData, &chipConfigListener);
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);
	sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

	// Device related configuration has its own sub-node.
	//DYNAPSE_CONFIG_DYNAPSE_U0
	sshsNode deviceConfigNodeU0 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U0, true));

	sshsNode biasNodeU0 = sshsGetRelativeNode(deviceConfigNodeU0, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodesU0 = sshsNodeGetChildren(biasNodeU0, &biasNodesLength);

	if (biasNodesU0 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodesU0[i], moduleData, &biasConfigListener);
		}

		free(biasNodesU0);
	}

	// Device related configuration has its own sub-node.
	//DYNAPSE_CONFIG_DYNAPSE_U1
	sshsNode deviceConfigNodeU1 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U1, true));

	sshsNode biasNodeU1 = sshsGetRelativeNode(deviceConfigNodeU1, "bias/");

	biasNodesLength = 0;
	sshsNode *biasNodesU1 = sshsNodeGetChildren(biasNodeU1, &biasNodesLength);

	if (biasNodesU1 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodesU1[i], moduleData, &biasConfigListener);
		}

		free(biasNodesU1);
	}

	//DYNAPSE_CONFIG_DYNAPSE_U2
	sshsNode deviceConfigNodeU2 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U2, true));

	sshsNode biasNodeU2 = sshsGetRelativeNode(deviceConfigNodeU2, "bias/");

	biasNodesLength = 0;
	sshsNode *biasNodesU2 = sshsNodeGetChildren(biasNodeU2, &biasNodesLength);

	if (biasNodesU2 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodesU2[i], moduleData, &biasConfigListener);
		}
		free(biasNodesU2);
	}

	//DYNAPSE_CONFIG_DYNAPSE_U3
	sshsNode deviceConfigNodeU3 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U3, true));

	sshsNode biasNodeU3 = sshsGetRelativeNode(deviceConfigNodeU3, "bias/");

	biasNodesLength = 0;
	sshsNode *biasNodesU3 = sshsNodeGetChildren(biasNodeU3, &biasNodesLength);

	if (biasNodesU3 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodesU3[i], moduleData, &biasConfigListener);
		}
		free(biasNodesU3);
	}

	//spike Generator Node
	sshsNode spikeNode = sshsGetRelativeNode(deviceConfigNode, "spikeGen/");
	sshsNodeAddAttributeListener(spikeNode, state, &spikeConfigListener);
	caerGenSpikeInit(moduleData); // init module and start thread

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, false);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, false);
	sleep(1); // essential
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP,
	DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 0, 0); // core 0 neuron 0
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 1, 5); //  core 1 neuron 5
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 2, 60); // core 2 neuron 10
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 3, 105); // core 3 neuron 20

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP,
	DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 0, 0); // core 0 neuron 0
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 1, 5); //  core 1 neuron 5
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 2, 60); // core 2 neuron 10
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 3, 105); // core 3 neuron 20

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP,
	DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 0, 0); // core 0 neuron 0
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 1, 5); //  core 1 neuron 5
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 2, 60); // core 2 neuron 10
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 3, 105); // core 3 neuron 20

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP,
	DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 0, 10); // core 0 neuron 0
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 1, 5); //  core 1 neuron 5
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 2, 60); // core 2 neuron 10
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_MONITOR_NEU, 3, 105); // core 3 neuron 20

	// Start data acquisition.
	bool ret = caerDeviceDataStart(state->deviceState, &caerMainloopDataNotifyIncrease, &caerMainloopDataNotifyDecrease,
	NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &state->deviceState);

		return (false);
	}

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);

}

void caerInputDYNAPSEExit(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	//struct caer_dynapse_info devInfo = caerDynapseInfoGet(
	//	((caerInputDynapseState) moduleData->moduleState)->deviceState);
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeRemoveAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeRemoveAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	// make sure no spikes are being sent
	sshsNode spikeNode = sshsGetRelativeNode(deviceConfigNode, "spikeGen/");
	sshsNodePutBool(spikeNode, "doStim", false);
	sshsNodePutBool(spikeNode, "doStimPrimitiveBias", false);
	sshsNodePutBool(spikeNode, "doStimPrimitiveCam", false);
	sshsNodeRemoveAttributeListener(spikeNode, moduleData, &spikeConfigListener);

	// Remove USB config listener for biases
	//DYNAPSE_CONFIG_DYNAPSE_U0
	sshsNode deviceConfigNodeU0 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U0, true));
	sshsNode biasNodeU0 = sshsGetRelativeNode(deviceConfigNodeU0, "bias/");
	size_t biasNodesLength = 0;
	sshsNode *biasNodesU0 = sshsNodeGetChildren(biasNodeU0, &biasNodesLength);
	if (biasNodesU0 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodesU0[i], moduleData, &biasConfigListener);
		}
		free(biasNodesU0);
	}

	//DYNAPSE_CONFIG_DYNAPSE_U1
	sshsNode deviceConfigNodeU1 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U1, true));
	sshsNode biasNodeU1 = sshsGetRelativeNode(deviceConfigNodeU1, "bias/");
	biasNodesLength = 0;
	sshsNode *biasNodesU1 = sshsNodeGetChildren(biasNodeU1, &biasNodesLength);
	if (biasNodesU1 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodesU1[i], moduleData, &biasConfigListener);
		}
		free(biasNodesU1);
	}

	//DYNAPSE_CONFIG_DYNAPSE_U2
	sshsNode deviceConfigNodeU2 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U2, true));
	sshsNode biasNodeU2 = sshsGetRelativeNode(deviceConfigNodeU2, "bias/");
	biasNodesLength = 0;
	sshsNode *biasNodesU2 = sshsNodeGetChildren(biasNodeU2, &biasNodesLength);
	if (biasNodesU2 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodesU2[i], moduleData, &biasConfigListener);
		}
		free(biasNodesU2);
	}

	//DYNAPSE_CONFIG_DYNAPSE_U3
	sshsNode deviceConfigNodeU3 = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U3, true));
	sshsNode biasNodeU3 = sshsGetRelativeNode(deviceConfigNodeU3, "bias/");
	biasNodesLength = 0;
	sshsNode *biasNodesU3 = sshsNodeGetChildren(biasNodeU3, &biasNodesLength);
	if (biasNodesU3 != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodesU3[i], moduleData, &biasConfigListener);
		}
		free(biasNodesU3);
	}

	caerDeviceDataStop(((caerInputDynapseState) moduleData->moduleState)->deviceState);
	//caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);
	caerDeviceClose(&(((caerInputDynapseState) moduleData->moduleState)->deviceState));

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}

	caerGenSpikeExit(moduleData);
}

void caerInputDYNAPSERun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	*out = caerDeviceDataGet(((caerInputDynapseState) moduleData->moduleState)->deviceState);

	if (*out != NULL) {
		sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
		sshsNodeUpdateReadOnlyAttribute(sourceInfoNode, "highestTimestamp", SSHS_LONG, (union sshs_node_attr_value ) {
				.ilong = caerEventPacketContainerGetHighestEventTimestamp(*out) });

		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindEventByType((caerSpecialEventPacket) special, TIMESTAMP_RESET) != NULL)) {
			caerMainloopResetProcessors(moduleData->moduleID);
			caerMainloopResetOutputs(moduleData->moduleID);

			// Update master/slave information.
			struct caer_dynapse_info devInfo = caerDynapseInfoGet(
				((caerInputDynapseState) moduleData->moduleState)->deviceState);
			sshsNodeUpdateReadOnlyAttribute(sourceInfoNode, "deviceIsMaster", SSHS_BOOL, (union sshs_node_attr_value ) {
					.boolean = devInfo.deviceIsMaster });
		}
	}
}


void caerDynapseSetBias(caerInputDynapseState state, uint32_t chipId, uint32_t coreId, const char *biasName_t,
	uint8_t coarseValue, uint16_t fineValue, const char *lowHigh, const char *npBias) {

	// Check if the pointer is valid.
	if (state->deviceState == NULL) {
		//struct caer_dynapse_info emptyInfo = { 0, .deviceString = NULL };
		return;
	}

	size_t biasNameLength = strlen(biasName_t);
	char biasName[biasNameLength + 3];

	biasName[0] = 'C';
	if (coreId == 0)
		biasName[1] = '0';
	else if (coreId == 1)
		biasName[1] = '1';
	else if (coreId == 2)
		biasName[1] = '2';
	else if (coreId == 3)
		biasName[1] = '3';
	biasName[2] = '_';

	uint32_t i;
	for (i = 0; i < biasNameLength + 3; i++) {
		biasName[3 + i] = biasName_t[i];
	}

	if (chipId != DYNAPSE_CONFIG_DYNAPSE_U0 && chipId != DYNAPSE_CONFIG_DYNAPSE_U1
		&& chipId != DYNAPSE_CONFIG_DYNAPSE_U2 && chipId != DYNAPSE_CONFIG_DYNAPSE_U3) {
		caerLog(CAER_LOG_ERROR, __func__, "Chip id is not valid %d", chipId);
	}
	else {
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, biasName, coarseValue, fineValue, lowHigh,
			"Normal", npBias, true, chipId);
	}
}

// TODO: this is not used anywhere
//write neuron CAM when a synapse is built or modified
static bool setCamContent(caerInputDynapseState state, int16_t chipId, bool ei, bool fs, int16_t address,
	int8_t source_core, int8_t coreId, int16_t row, int16_t column) {

	// Check if the pointer is valid.
	if (state->deviceState == NULL) {
		//struct caer_dynapse_info emptyInfo = { 0, .deviceString = NULL };
		return (false);
	}

	uint32_t bits = (uint32_t) (ei << 29 | fs << 28 | (uint16_t) address << 20 | (uint8_t) source_core << 18 | 1 << 17
		| (uint8_t) coreId << 8 | (uint16_t) row << 5 | (uint16_t) column << 0);

	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, (uint32_t) chipId);
	caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, bits); //this is the 30 bits

	return (true);
}

static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BYTE && caerStrEquals(changeKey, "logLevel")) {
		caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
			U32T(changeValue.ibyte));
	}
}
