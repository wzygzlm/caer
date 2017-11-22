#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/special.h>
#include <libcaer/events/spike.h>
#include <libcaer/devices/dynapse.h>
#include "dynapse_utils.h"

static bool caerInputDYNAPSEInit(caerModuleData moduleData);
static void caerInputDYNAPSEExit(caerModuleData moduleData);
static void caerInputDYNAPSERun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);

static const struct caer_module_functions DynapseFunctions = { .moduleInit = &caerInputDYNAPSEInit, .moduleRun =
	&caerInputDYNAPSERun, .moduleConfig = NULL, .moduleExit = &caerInputDYNAPSEExit };

static const struct caer_event_stream_out DynapseOutputs[] = { { .type = SPECIAL_EVENT }, { .type = SPIKE_EVENT } };

static const struct caer_module_info DynapseInfo = { .version = 2, .name = "Dynapse", .description =
	"Connects to a Dynap-SE neuromorphic processor to get data.", .type = CAER_MODULE_INPUT, .memSize = 0, .functions =
	&DynapseFunctions, .inputStreams = NULL, .inputStreamsSize = 0, .outputStreams = DynapseOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DynapseOutputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DynapseInfo);
}

static void createDefaultBiasConfiguration(caerModuleData moduleData);
static void createDefaultLogicConfiguration(caerModuleData moduleData, struct caer_dynapse_info *devInfo);
static void sendDefaultConfiguration(caerModuleData moduleData);
static void moduleShutdownNotify(void *p);

static void biasConfigSend(sshsNode node, caerModuleData moduleData);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void muxConfigSend(sshsNode node, caerModuleData moduleData);
static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void spikesAERConfigSend(sshsNode node, caerModuleData moduleData);
static void spikesAERConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void configAERConfigSend(sshsNode node, caerModuleData moduleData);
static void configAERConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void usbConfigSend(sshsNode node, caerModuleData moduleData);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigSend(sshsNode node, caerModuleData moduleData);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void neuronMonitorSend(sshsNode node, caerModuleData moduleData);
static void neuronMonitorListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void sramControlListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void camControlListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void resetToDefaultBiasesListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static void statisticsPassthrough(void *userData, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value *value);

static void createDynapseBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
bool biasHigh, bool typeNormal, bool sexN, bool enabled);
static void setDynapseBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
bool biasHigh, bool typeNormal, bool sexN, bool enabled);
static void setDynapseBias(sshsNode biasNode, caerDeviceHandle cdh);
static uint8_t generateBiasAddress(const char *biasName, const char *coreName);
static void generateDefaultBiases(sshsNode biasNode, uint8_t chipId);
static void resetDefaultBiases(sshsNode biasNode, uint8_t chipId);

// Additional Dynap-SE special settings.
static const char *resetAllBiasesKey = "ResetAllBiasesToDefault";
static char resetBiasesKey[] = "ResetUxBiasesToDefault";
static char monitorKey[] = "Ux_Cy";
static const char *emptyAllKey = "EmptyAll";
static char emptyKey[] = "EmptyUx";
static const char *defaultAllKey = "DefaultAll";
static char defaultKey[] = "DefaultUx";

static bool caerInputDYNAPSEInit(caerModuleData moduleData) {
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

	// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	char *serialNumber = sshsNodeGetString(moduleData->moduleNode, "serialNumber");
	moduleData->moduleState = caerDeviceOpen(U16T(moduleData->moduleID), CAER_DEVICE_DYNAPSE,
		U8T(sshsNodeGetShort(moduleData->moduleNode, "busNumber")),
		U8T(sshsNodeGetShort(moduleData->moduleNode, "devAddress")), serialNumber);
	free(serialNumber);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Put global source information into SSHS.
	struct caer_dynapse_info devInfo = caerDynapseInfoGet(moduleData->moduleState);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateShort(sourceInfoNode, "logicVersion", devInfo.logicVersion, devInfo.logicVersion,
		devInfo.logicVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device FPGA logic version.");
	sshsNodeCreateShort(sourceInfoNode, "chipID", devInfo.chipID, devInfo.chipID, devInfo.chipID,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device chip identification number.");

	sshsNodeCreateBool(sourceInfoNode, "aerHasStatistics", devInfo.aerHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA AER bus statistics.");

	sshsNodeCreateBool(sourceInfoNode, "muxHasStatistics", devInfo.muxHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA Multiplexer statistics (USB event drops).");

	// Put source information for generic visualization, to be used to display and debug filter information.
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(U8T(devInfo.chipID), false));

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(U8T(devInfo.chipID), false));
	sourceString[sourceStringLength] = '\0';

	sshsNodeCreateString(sourceInfoNode, "sourceString", sourceString, sourceStringLength, sourceStringLength,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device source information.");

	// Generate sub-system string for module.
	size_t subSystemStringLength = (size_t) snprintf(NULL, 0, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber,
		devInfo.deviceUSBDeviceAddress);

	char subSystemString[subSystemStringLength + 1];
	snprintf(subSystemString, subSystemStringLength + 1, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber,
		devInfo.deviceUSBDeviceAddress);
	subSystemString[subSystemStringLength] = '\0';

	caerModuleSetSubSystemString(moduleData, subSystemString);

	// Ensure good defaults for data acquisition settings.
	// No blocking behavior due to mainloop notification, and no auto-start of
	// all producers to ensure cAER settings are respected.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, false);
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_START_PRODUCERS, false);
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_STOP_PRODUCERS, true);

	// Apply default configuration to device (silent biases).
	caerDeviceSendDefaultConfig(moduleData->moduleState);

	// Create default settings and send them to the device.
	createDefaultBiasConfiguration(moduleData);
	createDefaultLogicConfiguration(moduleData, &devInfo);
	sendDefaultConfiguration(moduleData);

	// Start data acquisition.
	bool ret = caerDeviceDataStart(moduleData->moduleState, &caerMainloopDataNotifyIncrease,
		&caerMainloopDataNotifyDecrease,
		NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNode muxNode = sshsGetRelativeNode(moduleData->moduleNode, "multiplexer/");
	sshsNodeAddAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode spikesAERNode = sshsGetRelativeNode(moduleData->moduleNode, "spikesAER/");
	sshsNodeAddAttributeListener(spikesAERNode, moduleData, &spikesAERConfigListener);

	sshsNode configAERNode = sshsGetRelativeNode(moduleData->moduleNode, "configAER/");
	sshsNodeAddAttributeListener(configAERNode, moduleData, &configAERConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");

	sshsNodeAddAttributeListener(biasNode, moduleData, &resetToDefaultBiasesListener);

	size_t chipNodesLength = 0;
	sshsNode *chipNodes = sshsNodeGetChildren(biasNode, &chipNodesLength);

	if (chipNodes != NULL) {
		for (size_t i = 0; i < chipNodesLength; i++) {
			size_t coreNodesLength = 0;
			sshsNode *coreNodes = sshsNodeGetChildren(chipNodes[i], &coreNodesLength);

			if (coreNodes != NULL) {
				for (size_t j = 0; j < coreNodesLength; j++) {
					size_t biasNodesLength = 0;
					sshsNode *biasNodes = sshsNodeGetChildren(coreNodes[j], &biasNodesLength);

					if (biasNodes != NULL) {
						for (size_t k = 0; k < biasNodesLength; k++) {
							// Add listener for this particular bias.
							sshsNodeAddAttributeListener(biasNodes[k], moduleData, &biasConfigListener);
						}

						free(biasNodes);
					}
				}

				free(coreNodes);
			}
		}

		free(chipNodes);
	}

	sshsNode neuronMonitorNode = sshsGetRelativeNode(moduleData->moduleNode, "NeuronMonitor/");
	sshsNodeAddAttributeListener(neuronMonitorNode, moduleData, &neuronMonitorListener);

	sshsNode sramControlNode = sshsGetRelativeNode(moduleData->moduleNode, "SRAM/");
	sshsNodeAddAttributeListener(sramControlNode, moduleData, &sramControlListener);

	sshsNode camControlNode = sshsGetRelativeNode(moduleData->moduleNode, "CAM/");
	sshsNodeAddAttributeListener(camControlNode, moduleData, &camControlListener);

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);
}

static void caerInputDYNAPSEExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode muxNode = sshsGetRelativeNode(moduleData->moduleNode, "multiplexer/");
	sshsNodeRemoveAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode spikesAERNode = sshsGetRelativeNode(moduleData->moduleNode, "spikesAER/");
	sshsNodeRemoveAttributeListener(spikesAERNode, moduleData, &spikesAERConfigListener);

	sshsNode configAERNode = sshsGetRelativeNode(moduleData->moduleNode, "configAER/");
	sshsNodeRemoveAttributeListener(configAERNode, moduleData, &configAERConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeRemoveAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");

	sshsNodeRemoveAttributeListener(biasNode, moduleData, &resetToDefaultBiasesListener);

	size_t chipNodesLength = 0;
	sshsNode *chipNodes = sshsNodeGetChildren(biasNode, &chipNodesLength);

	if (chipNodes != NULL) {
		for (size_t i = 0; i < chipNodesLength; i++) {
			size_t coreNodesLength = 0;
			sshsNode *coreNodes = sshsNodeGetChildren(chipNodes[i], &coreNodesLength);

			if (coreNodes != NULL) {
				for (size_t j = 0; j < coreNodesLength; j++) {
					size_t biasNodesLength = 0;
					sshsNode *biasNodes = sshsNodeGetChildren(coreNodes[j], &biasNodesLength);

					if (biasNodes != NULL) {
						for (size_t k = 0; k < biasNodesLength; k++) {
							// Remove listener for this particular bias.
							sshsNodeRemoveAttributeListener(biasNodes[k], moduleData, &biasConfigListener);
						}

						free(biasNodes);
					}
				}

				free(coreNodes);
			}
		}

		free(chipNodes);
	}

	sshsNode neuronMonitorNode = sshsGetRelativeNode(moduleData->moduleNode, "NeuronMonitor/");
	sshsNodeRemoveAttributeListener(neuronMonitorNode, moduleData, &neuronMonitorListener);

	sshsNode sramControlNode = sshsGetRelativeNode(moduleData->moduleNode, "SRAM/");
	sshsNodeRemoveAttributeListener(sramControlNode, moduleData, &sramControlListener);

	sshsNode camControlNode = sshsGetRelativeNode(moduleData->moduleNode, "CAM/");
	sshsNodeRemoveAttributeListener(camControlNode, moduleData, &camControlListener);

	// Remove statistics read modifiers.
	sshsNode statNode = sshsGetRelativeNode(moduleData->moduleNode, "statistics/");
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

static void caerInputDYNAPSERun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	*out = caerDeviceDataGet(moduleData->moduleState);

	if (*out != NULL) {
		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindEventByType((caerSpecialEventPacket) special, TIMESTAMP_RESET) != NULL)) {
			caerMainloopResetProcessors(moduleData->moduleID);
			caerMainloopResetOutputs(moduleData->moduleID);
		}
	}
}

static void createDefaultBiasConfiguration(caerModuleData moduleData) {
	// Chip biases, based on testing defaults.
	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");

	// Allow reset to default low-power biases.
	sshsNodeCreateBool(biasNode, resetAllBiasesKey, false, SSHS_FLAGS_NOTIFY_ONLY,
		"Reset all biases to the default low-power values.");

	for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
		resetBiasesKey[6] = (char) (48 + chipId);

		sshsNodeCreateBool(biasNode, resetBiasesKey, false, SSHS_FLAGS_NOTIFY_ONLY,
			"Reset biases to the default low-power values.");
	}

	// Generate biases with default values.
	for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
		generateDefaultBiases(biasNode, chipId);
	}
}

static void createDefaultLogicConfiguration(caerModuleData moduleData, struct caer_dynapse_info *devInfo) {
	// Subsystem 0: Multiplexer
	sshsNode muxNode = sshsGetRelativeNode(moduleData->moduleNode, "multiplexer/");

	sshsNodeCreateBool(muxNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable multiplexer state machine.");
	sshsNodeCreateBool(muxNode, "TimestampRun", true, SSHS_FLAGS_NORMAL, "Enable µs-timestamp generation.");
	sshsNodeCreateBool(muxNode, "TimestampReset", false, SSHS_FLAGS_NOTIFY_ONLY, "Reset timestamps to zero.");
	sshsNodeCreateBool(muxNode, "ForceChipBiasEnable", false, SSHS_FLAGS_NORMAL,
		"Force the chip's bias generator to be always ON.");
	sshsNodeCreateBool(muxNode, "DropSpikesAEROnTransferStall", false, SSHS_FLAGS_NORMAL,
		"Drop AER spike events when USB FIFO is full.");

	// Subsystem 1: Spikes AER
	sshsNode spikesAERNode = sshsGetRelativeNode(moduleData->moduleNode, "spikesAER/");

	sshsNodeCreateBool(spikesAERNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable spike events AER.");
	sshsNodeCreateShort(spikesAERNode, "AckDelay", 0, 0, (0x01 << 12) - 1, SSHS_FLAGS_NORMAL,
		"Delay AER ACK by this many cycles.");
	sshsNodeCreateShort(spikesAERNode, "AckExtension", 0, 0, (0x01 << 12) - 1, SSHS_FLAGS_NORMAL,
		"Extend AER ACK by this many cycles.");
	sshsNodeCreateBool(spikesAERNode, "WaitOnTransferStall", false, SSHS_FLAGS_NORMAL,
		"On event FIFO full, wait to ACK until again empty if true, or just continue ACKing if false.");
	sshsNodeCreateBool(spikesAERNode, "ExternalAERControl", false, SSHS_FLAGS_NORMAL,
		"Don't drive AER ACK pin from FPGA (spikesAER.Run must also be disabled).");

	// Subsystem 5: Configuration AER
	sshsNode configAERNode = sshsGetRelativeNode(moduleData->moduleNode, "configAER/");

	sshsNodeCreateBool(configAERNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable chip configuration AER.");
	sshsNodeCreateShort(configAERNode, "ReqDelay", 30, 0, (0x01 << 12) - 1, SSHS_FLAGS_NORMAL,
		"Delay AER REQ by this many cycles.");
	sshsNodeCreateShort(configAERNode, "ReqExtension", 30, 0, (0x01 << 12) - 1, SSHS_FLAGS_NORMAL,
		"Extend AER REQ by this many cycles.");

	// Subsystem 9: FX2/3 USB Configuration and USB buffer settings.
	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeCreateBool(usbNode, "Run", true, SSHS_FLAGS_NORMAL,
		"Enable the USB state machine (FPGA to USB data exchange).");
	sshsNodeCreateShort(usbNode, "EarlyPacketDelay", 8, 1, 8000, SSHS_FLAGS_NORMAL,
		"Send early USB packets if this timeout is reached (in 125µs time-slices).");

	sshsNodeCreateInt(usbNode, "BufferNumber", 8, 2, 128, SSHS_FLAGS_NORMAL, "Number of USB transfers.");
	sshsNodeCreateInt(usbNode, "BufferSize", 8192, 512, 32768, SSHS_FLAGS_NORMAL,
		"Size in bytes of data buffers for USB transfers.");

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");

	// Packet settings (size (in events) and time interval (in µs)).
	sshsNodeCreateInt(sysNode, "PacketContainerMaxPacketSize", 8192, 1, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Maximum packet size in events, when any packet reaches this size, the EventPacketContainer is sent for processing.");
	sshsNodeCreateInt(sysNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL,
		"Time interval in µs, each sent EventPacketContainer will span this interval.");

	// Ring-buffer setting (only changes value on module init/shutdown cycles).
	sshsNodeCreateInt(sysNode, "DataExchangeBufferSize", 64, 8, 1024, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer queue, used for transfers between data acquisition thread and mainloop.");

	// Neuron monitoring (one per core).
	sshsNode neuronMonitorNode = sshsGetRelativeNode(moduleData->moduleNode, "NeuronMonitor/");

	for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
		for (uint8_t coreId = 0; coreId < DYNAPSE_CONFIG_NUMCORES; coreId++) {
			monitorKey[1] = (char) (48 + chipId);
			monitorKey[4] = (char) (48 + coreId);
			sshsNodeCreateShort(neuronMonitorNode, monitorKey, 0, 0, (DYNAPSE_CONFIG_NUMNEURONS_CORE - 1),
				SSHS_FLAGS_NORMAL, "Monitor a specific neuron.");
		}
	}

	// SRAM reset (empty, default).
	sshsNode sramControlNode = sshsGetRelativeNode(moduleData->moduleNode, "SRAM/");

	sshsNodeCreateBool(sramControlNode, emptyAllKey, false, SSHS_FLAGS_NOTIFY_ONLY, "Reset all SRAMs to empty.");
	sshsNodeCreateBool(sramControlNode, defaultAllKey, false, SSHS_FLAGS_NOTIFY_ONLY,
		"Reset all SRAMs to default routing.");

	for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
		emptyKey[6] = (char) (48 + chipId);
		sshsNodeCreateBool(sramControlNode, emptyKey, false, SSHS_FLAGS_NOTIFY_ONLY, "Reset SRAM to empty.");

		defaultKey[8] = (char) (48 + chipId);
		sshsNodeCreateBool(sramControlNode, defaultKey, false, SSHS_FLAGS_NOTIFY_ONLY,
			"Reset SRAM to default routing.");
	}

	// CAM reset (empty).
	sshsNode camControlNode = sshsGetRelativeNode(moduleData->moduleNode, "CAM/");

	sshsNodeCreateBool(camControlNode, emptyAllKey, false, SSHS_FLAGS_NOTIFY_ONLY, "Reset all CAMs to empty.");

	for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
		emptyKey[6] = (char) (48 + chipId);
		sshsNodeCreateBool(camControlNode, emptyKey, false, SSHS_FLAGS_NOTIFY_ONLY, "Reset CAM to empty.");
	}

	// Device event statistics.
	if (devInfo->muxHasStatistics) {
		sshsNode statNode = sshsGetRelativeNode(moduleData->moduleNode, "statistics/");

		sshsNodeCreateLong(statNode, "muxDroppedAER", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped AER Spike events due to USB full.");
		sshsNodeCreateAttributePollTime(statNode, "muxDroppedAER", SSHS_LONG, 2);
		sshsNodeAddAttributeReadModifier(statNode, "muxDroppedAER", SSHS_LONG, moduleData->moduleState,
			&statisticsPassthrough);
	}

	if (devInfo->aerHasStatistics) {
		sshsNode statNode = sshsGetRelativeNode(moduleData->moduleNode, "statistics/");

		sshsNodeCreateLong(statNode, "aerEventsHandled", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of AER Spike events handled.");
		sshsNodeCreateAttributePollTime(statNode, "aerEventsHandled", SSHS_LONG, 2);
		sshsNodeAddAttributeReadModifier(statNode, "aerEventsHandled", SSHS_LONG, moduleData->moduleState,
			&statisticsPassthrough);

		sshsNodeCreateLong(statNode, "aerEventsDropped", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped events (groups of events).");
		sshsNodeCreateAttributePollTime(statNode, "aerEventsDropped", SSHS_LONG, 2);
		sshsNodeAddAttributeReadModifier(statNode, "aerEventsDropped", SSHS_LONG, moduleData->moduleState,
			&statisticsPassthrough);
	}
}

static void sendDefaultConfiguration(caerModuleData moduleData) {
	// Send cAER configuration to libcaer and device.
	// First enable AER buses.
	configAERConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "configAER/"), moduleData);
	spikesAERConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "spikesAER/"), moduleData);

	// Then send biases, as they need the AER buses running.
	biasConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "bias/"), moduleData);

	// Enable neuron monitoring (analog external).
	neuronMonitorSend(sshsGetRelativeNode(moduleData->moduleNode, "NeuronMonitor/"), moduleData);

	// Last enable USB/Multiplexer, so we don't get startup garbage events/timestamps.
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	usbConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "usb/"), moduleData);
	muxConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "multiplexer/"), moduleData);
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void biasConfigSend(sshsNode node, caerModuleData moduleData) {
	size_t chipNodesLength = 0;
	sshsNode *chipNodes = sshsNodeGetChildren(node, &chipNodesLength);

	if (chipNodes != NULL) {
		for (size_t i = 0; i < chipNodesLength; i++) {
			size_t coreNodesLength = 0;
			sshsNode *coreNodes = sshsNodeGetChildren(chipNodes[i], &coreNodesLength);

			if (coreNodes != NULL) {
				for (size_t j = 0; j < coreNodesLength; j++) {
					size_t biasNodesLength = 0;
					sshsNode *biasNodes = sshsNodeGetChildren(coreNodes[j], &biasNodesLength);

					if (biasNodes != NULL) {
						for (size_t k = 0; k < biasNodesLength; k++) {
							// Send this particular bias.
							setDynapseBias(biasNodes[k], moduleData->moduleState);
						}

						free(biasNodes);
					}
				}

				free(coreNodes);
			}
		}

		free(chipNodes);
	}
}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		setDynapseBias(node, moduleData->moduleState);
	}
}

static void muxConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_TIMESTAMP_RESET,
		sshsNodeGetBool(node, "TimestampReset"));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE,
		sshsNodeGetBool(node, "ForceChipBiasEnable"));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_DROP_AER_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropSpikesAEROnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_TIMESTAMP_RUN,
		sshsNodeGetBool(node, "TimestampRun"));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_RUN,
		sshsNodeGetBool(node, "Run"));
}

static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampReset")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_TIMESTAMP_RESET,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ForceChipBiasEnable")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropSpikesAEROnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX,
			DYNAPSE_CONFIG_MUX_DROP_AER_ON_TRANSFER_STALL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampRun")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_TIMESTAMP_RUN,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_RUN,
				changeValue.boolean);
		}
	}
}

static void spikesAERConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_ACK_DELAY,
		U32T(sshsNodeGetShort(node, "AckDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_ACK_EXTENSION,
		U32T(sshsNodeGetShort(node, "AckExtension")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_WAIT_ON_TRANSFER_STALL,
		U32T(sshsNodeGetBool(node, "WaitOnTransferStall")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_EXTERNAL_AER_CONTROL,
		U32T(sshsNodeGetBool(node, "ExternalAERControl")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN,
		sshsNodeGetBool(node, "Run"));
}

static void spikesAERConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "AckDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_ACK_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "AckExtension")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_ACK_EXTENSION,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "WaitOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_WAIT_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ExternalAERControl")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_EXTERNAL_AER_CONTROL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN,
				changeValue.boolean);
		}
	}
}

static void configAERConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_REQ_DELAY,
		U32T(sshsNodeGetShort(node, "ReqDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_REQ_EXTENSION,
		U32T(sshsNodeGetShort(node, "ReqExtension")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN,
		sshsNodeGetBool(node, "Run"));
}

static void configAERConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "ReqDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_REQ_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "ReqExtension")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_REQ_EXTENSION,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN,
				changeValue.boolean);
		}
	}
}

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
		U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
		U32T(sshsNodeGetInt(node, "BufferSize")));

	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY,
		U32T(sshsNodeGetShort(node, "EarlyPacketDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_RUN,
		sshsNodeGetBool(node, "Run"));
}

static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferNumber")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EarlyPacketDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_RUN,
				changeValue.boolean);
		}
	}
}

static void systemConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
	CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(sshsNodeGetInt(node, "PacketContainerMaxPacketSize")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
	CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(sshsNodeGetInt(node, "PacketContainerInterval")));

	// Changes only take effect on module start!
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_BUFFER_SIZE, U32T(sshsNodeGetInt(node, "DataExchangeBufferSize")));
}

static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerMaxPacketSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
			CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
			CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(changeValue.iint));
		}
	}
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

static void neuronMonitorSend(sshsNode node, caerModuleData moduleData) {
	for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);

		for (uint8_t coreId = 0; coreId < DYNAPSE_CONFIG_NUMCORES; coreId++) {
			monitorKey[1] = (char) (48 + chipId);
			monitorKey[4] = (char) (48 + coreId);

			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MONITOR_NEU, coreId,
				U32T(sshsNodeGetShort(node, monitorKey)));
		}
	}
}

static void neuronMonitorListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_SHORT) {
		// Parse changeKey to get chipId and coreId.
		uint8_t chipId = U8T(changeKey[1] - 48);
		uint8_t coreId = U8T(changeKey[4] - 48);

		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MONITOR_NEU, coreId, U32T(changeValue.ishort));
	}
}

static void sramControlListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && changeValue.boolean == true) {
		if (caerStrEquals(changeKey, emptyAllKey)) {
			// Empty all SRAMs.
			for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
			}
		}
		else if (caerStrEquals(changeKey, defaultAllKey)) {
			// Set all SRAMs to default routing.
			for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_DEFAULT_SRAM, chipId, 0);
			}
		}
		else if (caerStrEqualsUpTo(changeKey, emptyKey, 6)) {
			uint8_t chipId = U8T(changeKey[6] - 48);

			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
		}
		else if (caerStrEqualsUpTo(changeKey, defaultKey, 8)) {
			uint8_t chipId = U8T(changeKey[8] - 48);

			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_DEFAULT_SRAM, chipId, 0);
		}
	}
}

static void camControlListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && changeValue.boolean == true) {
		if (caerStrEquals(changeKey, emptyAllKey)) {
			// Empty all CAMs.
			for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);
			}
		}
		else if (caerStrEqualsUpTo(changeKey, emptyKey, 6)) {
			uint8_t chipId = U8T(changeKey[6] - 48);

			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);
		}
	}
}

static void resetToDefaultBiasesListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && changeValue.boolean == true) {
		if (caerStrEquals(changeKey, resetAllBiasesKey)) {
			sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");

			for (uint8_t chipId = 0; chipId < DYNAPSE_X4BOARD_NUMCHIPS; chipId++) {
				resetDefaultBiases(biasNode, chipId);
			}
		}
		else if (caerStrEqualsUpTo(changeKey, resetBiasesKey, 6)) {
			uint8_t chipId = U8T(changeKey[6] - 48);

			resetDefaultBiases(sshsGetRelativeNode(moduleData->moduleNode, "bias/"), chipId);
		}
	}
}

static void statisticsPassthrough(void *userData, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value *value) {
	UNUSED_ARGUMENT(type); // We know all statistics are always LONG.

	caerDeviceHandle handle = userData;

	uint64_t statisticValue = 0;

	if (caerStrEquals(key, "muxDroppedAER")) {
		caerDeviceConfigGet64(handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_STATISTICS_AER_DROPPED, &statisticValue);
	}
	else if (caerStrEquals(key, "aerEventsHandled")) {
		caerDeviceConfigGet64(handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_STATISTICS_EVENTS, &statisticValue);
	}
	else if (caerStrEquals(key, "aerEventsDropped")) {
		caerDeviceConfigGet64(handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_STATISTICS_EVENTS_DROPPED,
			&statisticValue);
	}

	value->ilong = I64T(statisticValue);
}

static void createDynapseBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
bool biasHigh, bool typeNormal, bool sexN, bool enabled) {
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
	sshsNodeCreateString(biasConfigNode, "sex", (sexN) ? ("N") : ("P"), 1, 1, SSHS_FLAGS_NORMAL, "Bias sex.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "sex", SSHS_STRING, "N,P", false);
	sshsNodeCreateString(biasConfigNode, "type", (typeNormal) ? ("Normal") : ("Cascode"), 6, 7, SSHS_FLAGS_NORMAL,
		"Bias type.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "type", SSHS_STRING, "Normal,Cascode", false);
	sshsNodeCreateString(biasConfigNode, "currentLevel", (biasHigh) ? ("High") : ("Low"), 3, 4, SSHS_FLAGS_NORMAL,
		"Bias current level.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "currentLevel", SSHS_STRING, "High,Low", false);
}

static void setDynapseBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
bool biasHigh, bool typeNormal, bool sexN, bool enabled) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Set bias settings.
	sshsNodePutByte(biasConfigNode, "coarseValue", I8T(coarseValue));
	sshsNodePutShort(biasConfigNode, "fineValue", I16T(fineValue));

	sshsNodePutBool(biasConfigNode, "enabled", enabled);
	sshsNodePutString(biasConfigNode, "sex", (sexN) ? ("N") : ("P"));
	sshsNodePutString(biasConfigNode, "type", (typeNormal) ? ("Normal") : ("Cascode"));
	sshsNodePutString(biasConfigNode, "currentLevel", (biasHigh) ? ("High") : ("Low"));
}

static void setDynapseBias(sshsNode biasNode, caerDeviceHandle cdh) {
	sshsNode coreNode = sshsNodeGetParent(biasNode);
	sshsNode chipNode = sshsNodeGetParent(coreNode);

	const char *biasName = sshsNodeGetName(biasNode);
	const char *coreName = sshsNodeGetName(coreNode);
	const char *chipName = sshsNodeGetName(chipNode);

	// Build up bias value from all its components.
	char *sexString = sshsNodeGetString(biasNode, "sex");
	char *typeString = sshsNodeGetString(biasNode, "type");
	char *currentLevelString = sshsNodeGetString(biasNode, "currentLevel");

	uint8_t biasAddress = generateBiasAddress(biasName, coreName);

	struct caer_bias_dynapse biasValue = { .biasAddress = biasAddress, .coarseValue = U8T(
		sshsNodeGetByte(biasNode, "coarseValue")), .fineValue = U8T(sshsNodeGetShort(biasNode, "fineValue")), .enabled =
		sshsNodeGetBool(biasNode, "enabled"), .sexN = caerStrEquals(sexString, "N"), .typeNormal = caerStrEquals(
		typeString, "Normal"), .biasHigh = caerStrEquals(currentLevelString, "High") };

	// Free strings to avoid memory leaks.
	free(sexString);
	free(typeString);
	free(currentLevelString);

	uint8_t chipId = DYNAPSE_CONFIG_DYNAPSE_U0;

	if (caerStrEquals(chipName, chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U1, false))) {
		chipId = DYNAPSE_CONFIG_DYNAPSE_U1;
	}
	else if (caerStrEquals(chipName, chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U2, false))) {
		chipId = DYNAPSE_CONFIG_DYNAPSE_U2;
	}
	else if (caerStrEquals(chipName, chipIDToName(DYNAPSE_CONFIG_DYNAPSE_U3, false))) {
		chipId = DYNAPSE_CONFIG_DYNAPSE_U3;
	}

	caerDeviceConfigSet(cdh, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);

	uint32_t biasBits = caerBiasDynapseGenerate(biasValue);

	caerDeviceConfigSet(cdh, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, biasBits);

	caerLog(CAER_LOG_DEBUG, "Dynap-SE biasing", "Sent 'bias/%s/%s/%s/' - chipId: %d, biasAddress: %d.", chipName,
		coreName, biasName, chipId, biasAddress);
}

static uint8_t generateBiasAddress(const char *biasName, const char *coreName) {
	// Base address depends on core.
	uint8_t biasAddressBase;

	if (caerStrEquals(coreName, coreIDToName(0, false))) {
		biasAddressBase = 0;
	}
	else if (caerStrEquals(coreName, coreIDToName(1, false))) {
		biasAddressBase = 1;
	}
	else if (caerStrEquals(coreName, coreIDToName(2, false))) {
		biasAddressBase = 64 + 0;
	}
	else if (caerStrEquals(coreName, coreIDToName(3, false))) {
		biasAddressBase = 64 + 1;
	}
	else if (caerStrEquals(coreName, "Global")) {
		// U/D (not part of core).
		if (caerStrEquals(biasName, "U_BUFFER")) {
			return (DYNAPSE_CONFIG_BIAS_U_BUFFER);
		}
		else if (caerStrEquals(biasName, "U_SSP")) {
			return (DYNAPSE_CONFIG_BIAS_U_SSP);
		}
		else if (caerStrEquals(biasName, "U_SSN")) {
			return (DYNAPSE_CONFIG_BIAS_U_SSN);
		}
		else if (caerStrEquals(biasName, "D_BUFFER")) {
			return (DYNAPSE_CONFIG_BIAS_D_BUFFER);
		}
		else if (caerStrEquals(biasName, "D_SSP")) {
			return (DYNAPSE_CONFIG_BIAS_D_SSP);
		}
		else if (caerStrEquals(biasName, "D_SSN")) {
			return (DYNAPSE_CONFIG_BIAS_D_SSN);
		}
		else {
			// Not possible.
			return (UINT8_MAX);
		}
	}
	else {
		// Not possible.
		return (UINT8_MAX);
	}

	if (caerStrEquals(biasName, "PULSE_PWLK_P")) {
		return U8T(biasAddressBase + 0);
	}
	else if (caerStrEquals(biasName, "PS_WEIGHT_INH_S_N")) {
		return U8T(biasAddressBase + 2);
	}
	else if (caerStrEquals(biasName, "PS_WEIGHT_INH_F_N")) {
		return U8T(biasAddressBase + 4);
	}
	else if (caerStrEquals(biasName, "PS_WEIGHT_EXC_S_N")) {
		return U8T(biasAddressBase + 6);
	}
	else if (caerStrEquals(biasName, "PS_WEIGHT_EXC_F_N")) {
		return U8T(biasAddressBase + 8);
	}
	else if (caerStrEquals(biasName, "IF_RFR_N")) {
		return U8T(biasAddressBase + 10);
	}
	else if (caerStrEquals(biasName, "IF_TAU1_N")) {
		return U8T(biasAddressBase + 12);
	}
	else if (caerStrEquals(biasName, "IF_AHTAU_N")) {
		return U8T(biasAddressBase + 14);
	}
	else if (caerStrEquals(biasName, "IF_CASC_N")) {
		return U8T(biasAddressBase + 16);
	}
	else if (caerStrEquals(biasName, "IF_TAU2_N")) {
		return U8T(biasAddressBase + 18);
	}
	else if (caerStrEquals(biasName, "IF_BUF_P")) {
		return U8T(biasAddressBase + 20);
	}
	else if (caerStrEquals(biasName, "IF_AHTHR_N")) {
		return U8T(biasAddressBase + 22);
	}
	else if (caerStrEquals(biasName, "IF_THR_N")) {
		return U8T(biasAddressBase + 24);
	}
	else if (caerStrEquals(biasName, "NPDPIE_THR_S_P")) {
		return U8T(biasAddressBase + 26);
	}
	else if (caerStrEquals(biasName, "NPDPIE_THR_F_P")) {
		return U8T(biasAddressBase + 28);
	}
	else if (caerStrEquals(biasName, "NPDPII_THR_F_P")) {
		return U8T(biasAddressBase + 30);
	}
	else if (caerStrEquals(biasName, "NPDPII_THR_S_P")) {
		return U8T(biasAddressBase + 32);
	}
	else if (caerStrEquals(biasName, "IF_NMDA_N")) {
		return U8T(biasAddressBase + 34);
	}
	else if (caerStrEquals(biasName, "IF_DC_P")) {
		return U8T(biasAddressBase + 36);
	}
	else if (caerStrEquals(biasName, "IF_AHW_P")) {
		return U8T(biasAddressBase + 38);
	}
	else if (caerStrEquals(biasName, "NPDPII_TAU_S_P")) {
		return U8T(biasAddressBase + 40);
	}
	else if (caerStrEquals(biasName, "NPDPII_TAU_F_P")) {
		return U8T(biasAddressBase + 42);
	}
	else if (caerStrEquals(biasName, "NPDPIE_TAU_F_P")) {
		return U8T(biasAddressBase + 44);
	}
	else if (caerStrEquals(biasName, "NPDPIE_TAU_S_P")) {
		return U8T(biasAddressBase + 46);
	}
	else if (caerStrEquals(biasName, "R2R_P")) {
		return U8T(biasAddressBase + 48);
	}

	// Not possible.
	return (UINT8_MAX);
}

static void generateDefaultBiases(sshsNode biasNode, uint8_t chipId) {
	sshsNode chipBiasNode = sshsGetRelativeNode(biasNode, chipIDToName(chipId, true));

	for (uint8_t coreId = 0; coreId < DYNAPSE_CONFIG_NUMCORES; coreId++) {
		sshsNode coreBiasNode = sshsGetRelativeNode(chipBiasNode, coreIDToName(coreId, true));

		createDynapseBiasSetting(coreBiasNode, "IF_BUF_P", 3, 80, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "IF_RFR_N", 3, 3, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_NMDA_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_DC_P", 1, 30, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "IF_TAU1_N", 7, 5, false, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_TAU2_N", 6, 100, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_THR_N", 4, 120, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_AHW_P", 7, 0, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "IF_AHTAU_N", 7, 35, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_AHTHR_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "IF_CASC_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "PULSE_PWLK_P", 3, 106, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_INH_S_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_INH_F_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_EXC_S_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_EXC_F_N", 7, 0, true, true, true, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPII_TAU_S_P", 7, 40, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPII_TAU_F_P", 7, 0, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPII_THR_S_P", 7, 40, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPII_THR_F_P", 7, 0, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPIE_TAU_S_P", 7, 0, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPIE_TAU_F_P", 7, 40, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPIE_THR_S_P", 7, 0, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "NPDPIE_THR_F_P", 7, 0, true, true, false, true);
		createDynapseBiasSetting(coreBiasNode, "R2R_P", 4, 85, true, true, false, true);
	}

	sshsNode globalBiasNode = sshsGetRelativeNode(chipBiasNode, "Global/");

	createDynapseBiasSetting(globalBiasNode, "D_BUFFER", 1, 2, true, true, false, true);
	createDynapseBiasSetting(globalBiasNode, "D_SSP", 0, 7, true, true, false, true);
	createDynapseBiasSetting(globalBiasNode, "D_SSN", 0, 15, true, true, false, true);
	createDynapseBiasSetting(globalBiasNode, "U_BUFFER", 1, 2, true, true, false, true);
	createDynapseBiasSetting(globalBiasNode, "U_SSP", 0, 7, true, true, false, true);
	createDynapseBiasSetting(globalBiasNode, "U_SSN", 0, 15, true, true, false, true);
}

static void resetDefaultBiases(sshsNode biasNode, uint8_t chipId) {
	sshsNode chipBiasNode = sshsGetRelativeNode(biasNode, chipIDToName(chipId, true));

	for (uint8_t coreId = 0; coreId < DYNAPSE_CONFIG_NUMCORES; coreId++) {
		sshsNode coreBiasNode = sshsGetRelativeNode(chipBiasNode, coreIDToName(coreId, true));

		setDynapseBiasSetting(coreBiasNode, "IF_BUF_P", 3, 80, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "IF_RFR_N", 3, 3, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_NMDA_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_DC_P", 1, 30, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "IF_TAU1_N", 7, 5, false, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_TAU2_N", 6, 100, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_THR_N", 4, 120, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_AHW_P", 7, 0, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "IF_AHTAU_N", 7, 35, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_AHTHR_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "IF_CASC_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "PULSE_PWLK_P", 3, 106, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_INH_S_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_INH_F_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_EXC_S_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "PS_WEIGHT_EXC_F_N", 7, 0, true, true, true, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPII_TAU_S_P", 7, 40, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPII_TAU_F_P", 7, 0, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPII_THR_S_P", 7, 40, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPII_THR_F_P", 7, 0, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPIE_TAU_S_P", 7, 0, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPIE_TAU_F_P", 7, 40, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPIE_THR_S_P", 7, 0, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "NPDPIE_THR_F_P", 7, 0, true, true, false, true);
		setDynapseBiasSetting(coreBiasNode, "R2R_P", 4, 85, true, true, false, true);
	}

	sshsNode globalBiasNode = sshsGetRelativeNode(chipBiasNode, "Global/");

	setDynapseBiasSetting(globalBiasNode, "D_BUFFER", 1, 2, true, true, false, true);
	setDynapseBiasSetting(globalBiasNode, "D_SSP", 0, 7, true, true, false, true);
	setDynapseBiasSetting(globalBiasNode, "D_SSN", 0, 15, true, true, false, true);
	setDynapseBiasSetting(globalBiasNode, "U_BUFFER", 1, 2, true, true, false, true);
	setDynapseBiasSetting(globalBiasNode, "U_SSP", 0, 7, true, true, false, true);
	setDynapseBiasSetting(globalBiasNode, "U_SSN", 0, 15, true, true, false, true);
}
