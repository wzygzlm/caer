#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/special.h>
#include <libcaer/events/polarity.h>
#include <libcaer/devices/dvs128.h>

static bool caerInputDVS128Init(caerModuleData moduleData);
static void caerInputDVS128Run(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
// CONFIG: Nothing to do here in the main thread!
// All configuration is asynchronous through SSHS listeners.
static void caerInputDVS128Exit(caerModuleData moduleData);

static const struct caer_module_functions DVS128Functions = { .moduleInit = &caerInputDVS128Init, .moduleRun =
	&caerInputDVS128Run, .moduleConfig = NULL, .moduleExit = &caerInputDVS128Exit };

static const struct caer_event_stream_out DVS128Outputs[] = { { .type = SPECIAL_EVENT }, { .type = POLARITY_EVENT } };

static const struct caer_module_info DVS128Info = { .version = 1, .name = "DVS128", .type = CAER_MODULE_INPUT,
	.memSize = 0, .functions = &DVS128Functions, .inputStreams = NULL, .inputStreamsSize = 0, .outputStreams =
		DVS128Outputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DVS128Outputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DVS128Info);
}

static void createDefaultConfiguration(caerModuleData moduleData);
static void sendDefaultConfiguration(caerModuleData moduleData);
static void moduleShutdownNotify(void *p);
static void biasConfigSend(sshsNode node, caerModuleData moduleData);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void dvsConfigSend(sshsNode node, caerModuleData moduleData);
static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void usbConfigSend(sshsNode node, caerModuleData moduleData);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigSend(sshsNode node, caerModuleData moduleData);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static bool caerInputDVS128Init(caerModuleData moduleData) {
	caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString, "Initializing module ...");

	// USB port/bus/SN settings/restrictions.
	// These can be used to force connection to one specific device at startup.
	sshsNodeCreateShort(moduleData->moduleNode, "busNumber", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(moduleData->moduleNode, "devAddress", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(moduleData->moduleNode, "serialNumber", "", 0, 8, SSHS_FLAGS_NORMAL);

	// Add auto-restart setting.
	sshsNodeCreateBool(moduleData->moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL);

	// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	char *serialNumber = sshsNodeGetString(moduleData->moduleNode, "serialNumber");
	moduleData->moduleState = caerDeviceOpen(moduleData->moduleID, CAER_DEVICE_DVS128,
		U8T(sshsNodeGetShort(moduleData->moduleNode, "busNumber")),
		U8T(sshsNodeGetShort(moduleData->moduleNode, "devAddress")), serialNumber);
	free(serialNumber);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Put global source information into SSHS.
	struct caer_dvs128_info devInfo = caerDVS128InfoGet(moduleData->moduleState);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodePutLong(sourceInfoNode, "highestTimestamp", -1);

	sshsNodePutShort(sourceInfoNode, "logicVersion", devInfo.logicVersion);
	sshsNodePutBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster);

	sshsNodePutShort(sourceInfoNode, "dvsSizeX", devInfo.dvsSizeX);
	sshsNodePutShort(sourceInfoNode, "dvsSizeY", devInfo.dvsSizeY);

	// Put source information for generic visualization, to be used to display and debug filter information.
	sshsNodePutShort(sourceInfoNode, "dataSizeX", devInfo.dvsSizeX);
	sshsNodePutShort(sourceInfoNode, "dataSizeY", devInfo.dvsSizeY);

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": DVS128\r\n", moduleData->moduleID);

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": DVS128\r\n", moduleData->moduleID);
	sourceString[sourceStringLength] = '\0';

	sshsNodePutString(sourceInfoNode, "sourceString", sourceString);

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

	// Create default settings and send them to the device.
	createDefaultConfiguration(moduleData);
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
	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");
	sshsNodeAddAttributeListener(biasNode, moduleData, &biasConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(moduleData->moduleNode, "dvs/");
	sshsNodeAddAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	return (true);
}

static void caerInputDVS128Exit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");
	sshsNodeRemoveAttributeListener(biasNode, moduleData, &biasConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(moduleData->moduleNode, "dvs/");
	sshsNodeRemoveAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeRemoveAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	caerDeviceDataStop(moduleData->moduleState);

	caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

static void caerInputDVS128Run(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	*out = caerDeviceDataGet(moduleData->moduleState);

	if (*out != NULL) {
		caerMainloopFreeAfterLoop((void (*)(void *)) &caerEventPacketContainerFree, *out);

		sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
		sshsNodePutLong(sourceInfoNode, "highestTimestamp", caerEventPacketContainerGetHighestEventTimestamp(*out));

		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindEventByType((caerSpecialEventPacket) special, TIMESTAMP_RESET) != NULL)) {
			caerMainloopResetProcessors(moduleData->moduleID);
			caerMainloopResetOutputs(moduleData->moduleID);

			// Update master/slave information.
			struct caer_dvs128_info devInfo = caerDVS128InfoGet(moduleData->moduleState);
			sshsNodePutBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster);
		}
	}
}

static void createDefaultConfiguration(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.

	// Set default biases, from DVS128Fast.xml settings.
	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");
	sshsNodeCreateInt(biasNode, "cas", 1992, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "injGnd", 1108364, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "reqPd", 16777215, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "puX", 8159221, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "diffOff", 132, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "req", 309590, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "refr", 969, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "puY", 16777215, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "diffOn", 209996, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "diff", 13125, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "foll", 271, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(biasNode, "pr", 217, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL);

	// DVS settings.
	sshsNode dvsNode = sshsGetRelativeNode(moduleData->moduleNode, "dvs/");
	sshsNodeCreateBool(dvsNode, "Run", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(dvsNode, "TimestampReset", false, SSHS_FLAGS_NOTIFY_ONLY);
	sshsNodeCreateBool(dvsNode, "ArrayReset", false, SSHS_FLAGS_NOTIFY_ONLY);

	// USB buffer settings.
	sshsNode usbNode = sshsGetRelativeNode(moduleData->moduleNode, "usb/");
	sshsNodeCreateInt(usbNode, "BufferNumber", 8, 2, 128, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(usbNode, "BufferSize", 4096, 512, 32768, SSHS_FLAGS_NORMAL);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");

	// Packet settings (size (in events) and time interval (in µs)).
	sshsNodeCreateInt(sysNode, "PacketContainerMaxPacketSize", 4096, 1, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(sysNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL);

	// Ring-buffer setting (only changes value on module init/shutdown cycles).
	sshsNodeCreateInt(sysNode, "DataExchangeBufferSize", 64, 8, 1024, SSHS_FLAGS_NORMAL);
}

static void sendDefaultConfiguration(caerModuleData moduleData) {
	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "bias/"), moduleData);
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	usbConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "usb/"), moduleData);
	dvsConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "dvs/"), moduleData);
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void biasConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_CAS,
		U32T(sshsNodeGetInt(node, "cas")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_INJGND,
		U32T(sshsNodeGetInt(node, "injGnd")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_REQPD,
		U32T(sshsNodeGetInt(node, "reqPd")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PUX,
		U32T(sshsNodeGetInt(node, "puX")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_DIFFOFF,
		U32T(sshsNodeGetInt(node, "diffOff")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_REQ,
		U32T(sshsNodeGetInt(node, "req")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_REFR,
		U32T(sshsNodeGetInt(node, "refr")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PUY,
		U32T(sshsNodeGetInt(node, "puY")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_DIFFON,
		U32T(sshsNodeGetInt(node, "diffOn")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_DIFF,
		U32T(sshsNodeGetInt(node, "diff")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_FOLL,
		U32T(sshsNodeGetInt(node, "foll")));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PR,
		U32T(sshsNodeGetInt(node, "pr")));
}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "cas")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_CAS,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "injGnd")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_INJGND,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "reqPd")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_REQPD,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "puX")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PUX,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "diffOff")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_DIFFOFF,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "req")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_REQ,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "refr")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_REFR,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "puY")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PUY,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "diffOn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_DIFFON,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "diff")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_DIFF,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "foll")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_FOLL,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "pr")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PR,
				U32T(changeValue.iint));
		}
	}
}

static void dvsConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_DVS, DVS128_CONFIG_DVS_ARRAY_RESET,
		sshsNodeGetBool(node, "ArrayReset"));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_DVS, DVS128_CONFIG_DVS_TIMESTAMP_RESET,
		sshsNodeGetBool(node, "TimestampReset"));
	caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_DVS, DVS128_CONFIG_DVS_RUN,
		sshsNodeGetBool(node, "Run"));
}

static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ArrayReset")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_DVS, DVS128_CONFIG_DVS_ARRAY_RESET,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampReset")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_DVS, DVS128_CONFIG_DVS_TIMESTAMP_RESET,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS128_CONFIG_DVS, DVS128_CONFIG_DVS_RUN, changeValue.boolean);
		}
	}
}

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
		U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
		U32T(sshsNodeGetInt(node, "BufferSize")));
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
