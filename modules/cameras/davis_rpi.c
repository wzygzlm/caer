#include "davis_utils.h"

static bool caerInputDAVISRPiInit(caerModuleData moduleData);
static void caerInputDAVISRPiExit(caerModuleData moduleData);

static const struct caer_module_functions DAVISRPiFunctions = { .moduleInit = &caerInputDAVISRPiInit, .moduleRun =
	&caerInputDAVISCommonRun, .moduleConfig = NULL, .moduleExit = &caerInputDAVISRPiExit };

static const struct caer_event_stream_out DAVISRPiOutputs[] = { { .type = SPECIAL_EVENT }, { .type = POLARITY_EVENT }, {
	.type = FRAME_EVENT }, { .type = IMU6_EVENT } };

static const struct caer_module_info DAVISRPiInfo = { .version = 1, .name = "DAVISRPi", .description =
	"Connects to a DAVIS Raspberry-Pi camera module to get data.", .type = CAER_MODULE_INPUT, .memSize = 0, .functions =
	&DAVISRPiFunctions, .inputStreams = NULL, .inputStreamsSize = 0, .outputStreams = DAVISRPiOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DAVISRPiOutputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DAVISRPiInfo);
}

static void createDefaultAERConfiguration(caerModuleData moduleData, const char *nodePrefix);
static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo);

static void aerConfigSend(sshsNode node, caerModuleData moduleData);
static void aerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static bool caerInputDAVISRPiInit(caerModuleData moduleData) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// Add auto-restart setting.
	sshsNodeCreateBool(moduleData->moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL,
		"Automatically restart module after shutdown.");

	// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	moduleData->moduleState = caerDeviceOpen(U16T(moduleData->moduleID), CAER_DEVICE_DAVIS_RPI, 0, 0, NULL);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Put global source information into SSHS.
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateShort(sourceInfoNode, "logicVersion", devInfo.logicVersion, devInfo.logicVersion,
		devInfo.logicVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device FPGA logic version.");
	sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Timestamp synchronization support: device master status.");
	sshsNodeCreateShort(sourceInfoNode, "chipID", devInfo.chipID, devInfo.chipID, devInfo.chipID,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device chip identification number.");

	sshsNodeCreateShort(sourceInfoNode, "polaritySizeX", devInfo.dvsSizeX, devInfo.dvsSizeX, devInfo.dvsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events width.");
	sshsNodeCreateShort(sourceInfoNode, "polaritySizeY", devInfo.dvsSizeY, devInfo.dvsSizeY, devInfo.dvsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events height.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasPixelFilter", devInfo.dvsHasPixelFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS Pixel-level filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasBackgroundActivityFilter", devInfo.dvsHasBackgroundActivityFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Device supports FPGA DVS Background-Activity and Refractory Period filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasTestEventGenerator", devInfo.dvsHasTestEventGenerator,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS Test-Event-Generator.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasROIFilter", devInfo.dvsHasROIFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS ROI filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasStatistics", devInfo.dvsHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS statistics.");

	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", devInfo.apsSizeX, devInfo.apsSizeX, devInfo.apsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", devInfo.apsSizeY, devInfo.apsSizeY, devInfo.apsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events height.");
	sshsNodeCreateByte(sourceInfoNode, "apsColorFilter", devInfo.apsColorFilter, devInfo.apsColorFilter,
		devInfo.apsColorFilter, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "APS sensor color-filter pattern.");
	sshsNodeCreateBool(sourceInfoNode, "apsHasGlobalShutter", devInfo.apsHasGlobalShutter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "APS sensor supports global-shutter mode.");
	sshsNodeCreateBool(sourceInfoNode, "apsHasQuadROI", devInfo.apsHasQuadROI,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "APS sensor supports up to four Regions-of-Interest.");
	sshsNodeCreateBool(sourceInfoNode, "apsHasExternalADC", devInfo.apsHasExternalADC,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Readout APS sensor using an external ADC chip.");
	sshsNodeCreateBool(sourceInfoNode, "apsHasInternalADC", devInfo.apsHasInternalADC,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Readout APS sensor using chip-internal ADC.");

	sshsNodeCreateBool(sourceInfoNode, "extInputHasGenerator", devInfo.extInputHasGenerator,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports generating pulses on output signal jack.");
	sshsNodeCreateBool(sourceInfoNode, "extInputHasExtraDetectors", devInfo.extInputHasExtraDetectors,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports extra signal detectors on additional pins.");

	sshsNodeCreateBool(sourceInfoNode, "muxHasStatistics", devInfo.muxHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA Multiplexer statistics (USB event drops).");

	// Put source information for generic visualization, to be used to display and debug filter information.
	int16_t dataSizeX = (devInfo.dvsSizeX > devInfo.apsSizeX) ? (devInfo.dvsSizeX) : (devInfo.apsSizeX);
	int16_t dataSizeY = (devInfo.dvsSizeY > devInfo.apsSizeY) ? (devInfo.dvsSizeY) : (devInfo.apsSizeY);

	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", dataSizeX, dataSizeX, dataSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", dataSizeY, dataSizeY, dataSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(devInfo.chipID, false));

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(devInfo.chipID, false));
	sourceString[sourceStringLength] = '\0';

	sshsNodeCreateString(sourceInfoNode, "sourceString", sourceString, sourceStringLength, sourceStringLength,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device source information.");

	// Ensure good defaults for data acquisition settings.
	// No blocking behavior due to mainloop notification, and no auto-start of
	// all producers to ensure cAER settings are respected.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, false);
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_START_PRODUCERS, false);
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_STOP_PRODUCERS, true);

	// Create default settings and send them to the device.
	createDefaultBiasConfiguration(moduleData, chipIDToName(devInfo.chipID, true), devInfo.chipID);
	createDefaultLogicConfiguration(moduleData, chipIDToName(devInfo.chipID, true), &devInfo);
	createDefaultAERConfiguration(moduleData, chipIDToName(devInfo.chipID, true));
	sendDefaultConfiguration(moduleData, &devInfo);

	// Start data acquisition.
	bool ret = caerDeviceDataStart(moduleData->moduleState, &caerMainloopDataNotifyIncrease,
		&caerMainloopDataNotifyDecrease,
		NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

		return (false);
	}

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo.chipID, true));

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeAddAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeAddAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeAddAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");
	sshsNodeAddAttributeListener(apsNode, moduleData, &apsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeAddAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeAddAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode aerNode = sshsGetRelativeNode(deviceConfigNode, "aer/");
	sshsNodeAddAttributeListener(aerNode, moduleData, &aerConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodes = sshsNodeGetChildren(biasNode, &biasNodesLength);

	if (biasNodes != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
		}

		free(biasNodes);
	}

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);
}

static void caerInputDAVISRPiExit(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo.chipID, true));

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeRemoveAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeRemoveAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeRemoveAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");
	sshsNodeRemoveAttributeListener(apsNode, moduleData, &apsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeRemoveAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeRemoveAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode aerNode = sshsGetRelativeNode(deviceConfigNode, "aer/");
	sshsNodeRemoveAttributeListener(aerNode, moduleData, &aerConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodes = sshsNodeGetChildren(biasNode, &biasNodesLength);

	if (biasNodes != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Remove listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
		}

		free(biasNodes);
	}

	// Ensure Exposure value is coherent with libcaer. Removing a Read Modifier
	// will synchronize the value once here on exit.
	sshsNodeRemoveAttributeReadModifier(apsNode, "Exposure", SSHS_INT);

	// Remove statistics read modifiers.
	sshsNode statNode = sshsGetRelativeNode(deviceConfigNode, "statistics/");
	sshsNodeRemoveAllAttributeReadModifiers(statNode);

	caerDeviceDataStop(moduleData->moduleState);

	caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

static void createDefaultAERConfiguration(caerModuleData moduleData, const char *nodePrefix) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, nodePrefix);

	// Subsystem 9: DDR AER output configuration.
	sshsNode aerNode = sshsGetRelativeNode(deviceConfigNode, "aer/");
	sshsNodeCreateBool(aerNode, "Run", true, SSHS_FLAGS_NORMAL,
		"Enable the DDR AER output state machine (FPGA to Raspberry-Pi data exchange).");
	sshsNodeCreateShort(aerNode, "ReqDelay", 1, 0, (0x01 << 10) - 1, SSHS_FLAGS_NORMAL,
		"Delay AER REQ by this many cycles after data output.");
	sshsNodeCreateShort(aerNode, "AckDelay", 1, 0, (0x01 << 10) - 1, SSHS_FLAGS_NORMAL,
		"Delay reacting to AER ACK by this many cycles.");
}

static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(deviceConfigNode, "bias/"), moduleData, devInfo);
	chipConfigSend(sshsGetRelativeNode(deviceConfigNode, "chip/"), moduleData, devInfo);
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	aerConfigSend(sshsGetRelativeNode(deviceConfigNode, "aer/"), moduleData);
	muxConfigSend(sshsGetRelativeNode(deviceConfigNode, "multiplexer/"), moduleData);
	dvsConfigSend(sshsGetRelativeNode(deviceConfigNode, "dvs/"), moduleData, devInfo);
	apsConfigSend(sshsGetRelativeNode(deviceConfigNode, "aps/"), moduleData, devInfo);
	imuConfigSend(sshsGetRelativeNode(deviceConfigNode, "imu/"), moduleData);
	extInputConfigSend(sshsGetRelativeNode(deviceConfigNode, "externalInput/"), moduleData, devInfo);
}

static void aerConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_REQ_DELAY,
		U32T(sshsNodeGetShort(node, "ReqDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_ACK_DELAY,
		U32T(sshsNodeGetShort(node, "AckDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN,
		sshsNodeGetBool(node, "Run"));
}

static void aerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "ReqDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_REQ_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "AckDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_ACK_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN,
				changeValue.boolean);
		}
	}
}
