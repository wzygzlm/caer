/* Caffe Interface cAER module
 *  Author: federico.corradi@inilabs.com
 */

#include "base/mainloop.h"
#include "base/module.h"
#include "wrapper.h"

struct caffewrapper_state {
	uint32_t *integertest;
	char * file_to_classify;
	double detThreshold;
	bool doPrintOutputs;
	bool doShowActivations;
	bool doNormInputImages;
	struct MyCaffe* cpp_class; //pointer to cpp_class_object
};

typedef struct caffewrapper_state *caffewrapperState;

static bool caerCaffeWrapperInit(caerModuleData moduleData);
static void caerCaffeWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerCaffeWrapperExit(caerModuleData moduleData);

static struct caer_module_functions caerCaffeWrapperFunctions = { .moduleInit = &caerCaffeWrapperInit, .moduleRun =
	&caerCaffeWrapperRun, .moduleConfig =
NULL, .moduleExit = &caerCaffeWrapperExit };


static const struct caer_event_stream_in moduleInputs[] = {
    { .type = FRAME_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "CaffeInterface",
	.description = "Caffe Deep Learning Interface",
	.type = CAER_MODULE_OUTPUT,
	.memSize = sizeof(struct caffewrapper_state),
	.functions = &caerCaffeWrapperFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL,
	.outputStreamsSize = 0
};

// init

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}

static bool caerCaffeWrapperInit(caerModuleData moduleData) {
	caffewrapperState state = moduleData->moduleState;

	sshsNodeCreateDouble(moduleData->moduleNode, "detThreshold", 0.96, 0.1, 1.0, SSHS_FLAGS_NORMAL, "Detection Threshold");
	sshsNodeCreateBool(moduleData->moduleNode, "doPrintOutputs", false, SSHS_FLAGS_NORMAL, "Print Outputs");
	sshsNodeCreateBool(moduleData->moduleNode, "doShowActivations", false, SSHS_FLAGS_NORMAL, "TODO");
	sshsNodeCreateBool(moduleData->moduleNode, "doNormInputImages", true, SSHS_FLAGS_NORMAL, "Normalize input images, before inputting them into caffe range [0,1]");
	sshsNodeCreateInt(moduleData->moduleNode, "sizeDisplay", 1024, 128, 10240, SSHS_FLAGS_NORMAL, "Display Size Set");

	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode, "detThreshold");
	state->doPrintOutputs = sshsNodeGetBool(moduleData->moduleNode, "doPrintOutputs");
	state->doShowActivations = sshsNodeGetBool(moduleData->moduleNode, "doShowActivations");
	state->doNormInputImages = sshsNodeGetBool(moduleData->moduleNode, "doNormInputImages");

	//Initializing caffe network..
	state->cpp_class = newMyCaffe();
	MyCaffe_init_network(state->cpp_class);

	//allocate single frame

	return (true);
}

static void caerCaffeWrapperExit(caerModuleData moduleData) {
	caffewrapperState state = moduleData->moduleState;
	deleteMyCaffe(state->cpp_class); //free memory block
}

static void caerCaffeWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {

	caerFrameEventPacketConst frameIn =
			(caerFrameEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, FRAME_EVENT);

	caffewrapperState state = moduleData->moduleState;

	//update module state
	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode, "detThreshold");
	state->doPrintOutputs = sshsNodeGetBool(moduleData->moduleNode, "doPrintOutputs");
	state->doShowActivations = sshsNodeGetBool(moduleData->moduleNode, "doShowActivations");
	state->doNormInputImages = sshsNodeGetBool(moduleData->moduleNode, "doNormInputImages");


	MyCaffe_file_set(state->cpp_class, frameIn, state->detThreshold, state->doPrintOutputs,
		state->doShowActivations, state->doNormInputImages);

	/*networkActivity = caerFrameEventPacketAllocate(1, I16T(moduleData->moduleID), 0, frame_x, frame_y, 1);
	caerMainloopFreeAfterLoop(&free, *networkActivity);
	if (*networkActivity != NULL) {
		caerFrameEvent single_frame = caerFrameEventPacketGetEvent(*networkActivity, 0);
		//add info to the frame
		caerFrameEventSetLengthXLengthYChannelNumber(single_frame, frame_x, frame_y, 1, *networkActivity); // to do remove hard coded size
		MyCaffe_file_set(state->cpp_class, hist, size, classificationResults, classificationResultsId, state->detThreshold,
			state->doPrintOutputs, &single_frame, state->doShowActivations, state->doNormInputImages);
		// validate frame
		if (single_frame != NULL) {
			caerFrameEventValidate(single_frame, *networkActivity);
		}
		else {
			*networkActivity = NULL;
		}

	}
	return;*/
}
