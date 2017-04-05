#include "davis_common.h"

static bool caerInputDAVISFX2Init(caerModuleData moduleData);
// RUN: common to all DAVIS systems.
// CONFIG: Nothing to do here in the main thread!
// All configuration is asynchronous through SSHS listeners.
// EXIT: common to all DAVIS systems.

static const struct caer_module_functions DAVISFX2Functions = { .moduleInit = &caerInputDAVISFX2Init, .moduleRun =
	&caerInputDAVISRun, .moduleConfig = NULL, .moduleExit = &caerInputDAVISExit };

static const struct caer_event_stream DAVISFX2Outputs[] = { { .type = SPECIAL_EVENT, .number = 1 }, { .type =
	POLARITY_EVENT, .number = 1 }, { .type = FRAME_EVENT, .number = 1 }, { .type = IMU6_EVENT, .number = 1 } };

static const struct caer_module_info DAVISFX2Info = { .version = 1, .name = "DAVISFX2", .type = CAER_MODULE_INPUT,
	.memSize = 0, .functions = &DAVISFX2Functions, .inputStreams = NULL, .inputStreamsSize = 0, .outputStreams =
		DAVISFX2Outputs, .outputStreamsSize = CAER_EVENT_STREAM_SIZE(DAVISFX2Outputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&DAVISFX2Info);
}

static bool caerInputDAVISFX2Init(caerModuleData moduleData) {
	return (caerInputDAVISInit(moduleData, CAER_DEVICE_DAVIS_FX2));
}
