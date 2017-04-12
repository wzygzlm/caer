#include "davis_common.h"

static bool caerInputDAVISFX3Init(caerModuleData moduleData);
// RUN: common to all DAVIS systems.
// CONFIG: Nothing to do here in the main thread!
// All configuration is asynchronous through SSHS listeners.
// EXIT: common to all DAVIS systems.

static const struct caer_module_functions DAVISFX3Functions = { .moduleInit = &caerInputDAVISFX3Init, .moduleRun =
	&caerInputDAVISRun, .moduleConfig = NULL, .moduleExit = &caerInputDAVISExit };

static const struct caer_event_stream_out DAVISFX3Outputs[] = { { .type = SPECIAL_EVENT, .number = 1 }, { .type =
	POLARITY_EVENT, .number = 1 }, { .type = FRAME_EVENT, .number = 1 }, { .type = IMU6_EVENT, .number = 1 }, { .type =
	SAMPLE_EVENT, .number = 1 } };

static const struct caer_module_info DAVISFX3Info = { .version = 1, .name = "DAVISFX3", .type = CAER_MODULE_INPUT,
	.memSize = 0, .functions = &DAVISFX3Functions, .inputStreams = NULL, .inputStreamsSize = 0, .outputStreams =
		DAVISFX3Outputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DAVISFX3Outputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DAVISFX3Info);
}

static bool caerInputDAVISFX3Init(caerModuleData moduleData) {
	return (caerInputDAVISInit(moduleData, CAER_DEVICE_DAVIS_FX3));
}
