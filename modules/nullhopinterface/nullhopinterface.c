/* NullHop Zynq Interface cAER module
 *  Author: federico.corradi@inilabs.com
 */

//const char * caerNullHopWrapper(uint16_t moduleID, int * hist_packet, bool * haveimg, int * result);

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "wrapper.h"
#include <sys/types.h>
#include <sys/wait.h>

#include <libcaer/events/frame.h>

struct nullhopwrapper_state {
	double detThreshold;
	struct MyClass* cpp_class; //pointer to cpp_class_object
};

typedef struct nullhopwrapper_state *nullhopwrapperState;

static bool caerNullHopWrapperInit(caerModuleData moduleData);
static void caerNullHopWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerNullHopWrapperExit(caerModuleData moduleData);

static struct caer_module_functions caerNullHopWrapperFunctions = {
		.moduleInit = &caerNullHopWrapperInit, .moduleRun =
				&caerNullHopWrapperRun, .moduleConfig =
		NULL, .moduleExit = &caerNullHopWrapperExit };


static const struct caer_event_stream_in moduleInputs[] = {
    { .type = FRAME_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "Nullhop Interface",
	.description = "NullHop interface",
	.type = CAER_MODULE_OUTPUT,
	.memSize = sizeof(struct nullhopwrapper_state),
	.functions = &caerNullHopWrapperFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL,
	.outputStreamsSize = 0
};

// init

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}

static bool caerNullHopWrapperInit(caerModuleData moduleData) {

	nullhopwrapperState state = moduleData->moduleState;
	sshsNodeCreateDouble(moduleData->moduleNode, "detThreshold", 0.5, 0.1, 1, SSHS_FLAGS_NORMAL, "Detection Threshold");

	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode,
			"detThreshold");

	//Initializing nullhop network..
	state->cpp_class = newzs_driver("modules/nullhopinterface/nets/roshamboNet_v3.nhp");

	return (true);
}

static void caerNullHopWrapperExit(caerModuleData moduleData) {
	nullhopwrapperState state = moduleData->moduleState;

	//zs_driverMonitor_closeThread(state->cpp_class); // join
	//deleteMyClass(state->cpp_class); //free memory block
}

static void caerNullHopWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {

	caerFrameEventPacketConst frameIn =
			(caerFrameEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, FRAME_EVENT);


	//int * imagestreamer_hists = va_arg(args, int*);
	//bool * haveimg = va_arg(args, bool*);
	//int * result = va_arg(args, int*);

	if (frameIn == NULL) {
		return;
	}

	nullhopwrapperState state = moduleData->moduleState;

	//update module state
	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode,
			"detThreshold");

	zs_driver_classify_image(state->cpp_class, frameIn);

}
