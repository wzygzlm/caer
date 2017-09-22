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
	int lowPassNumber;
	struct MyCaffe* cpp_class; //pointer to cpp_class_object
};

typedef struct caffewrapper_state *caffewrapperState;

static bool caerCaffeWrapperInit(caerModuleData moduleData);
static void caerCaffeWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerCaffeWrapperExit(caerModuleData moduleData);
static void caerCaffeWrapperUpdateConfigs(caerModuleData moduleData);

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
	sshsNodeCreateInt(moduleData->moduleNode, "lowPassNumers", 3, 0, 20, SSHS_FLAGS_NORMAL, "Number of decision that will be used to average over (lowpass)");
	sshsNodeCreateBool(moduleData->moduleNode, "doPrintOutputs", false, SSHS_FLAGS_NORMAL, "Print Outputs");
	sshsNodeCreateBool(moduleData->moduleNode, "doShowActivations", false, SSHS_FLAGS_NORMAL, "TODO");
	sshsNodeCreateBool(moduleData->moduleNode, "doNormInputImages", true, SSHS_FLAGS_NORMAL, "Normalize input images, before inputting them into caffe range [0,1]");
	sshsNodeCreateInt(moduleData->moduleNode, "sizeDisplay", 1024, 128, 10240, SSHS_FLAGS_NORMAL, "Display Size Set");

	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode, "detThreshold");
	state->doPrintOutputs = sshsNodeGetBool(moduleData->moduleNode, "doPrintOutputs");
	state->doShowActivations = sshsNodeGetBool(moduleData->moduleNode, "doShowActivations");
	state->doNormInputImages = sshsNodeGetBool(moduleData->moduleNode, "doNormInputImages");
	state->lowPassNumber = sshsNodeGetInt(moduleData->moduleNode, "lowPassNumers");

	//Initializing caffe network..
	state->cpp_class = newMyCaffe();
	MyCaffe_init_network(state->cpp_class, state->lowPassNumber); // number of average decisions

	//allocate single frame

	return (true);
}

static void caerCaffeWrapperExit(caerModuleData moduleData) {
	caffewrapperState state = moduleData->moduleState;
	deleteMyCaffe(state->cpp_class); //free memory block
}

static void caerCaffeWrapperUpdateConfigs(caerModuleData moduleData){

	caffewrapperState state = moduleData->moduleState;

	//update module state
	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode, "detThreshold");
	state->doPrintOutputs = sshsNodeGetBool(moduleData->moduleNode, "doPrintOutputs");
	state->doShowActivations = sshsNodeGetBool(moduleData->moduleNode, "doShowActivations");
	state->doNormInputImages = sshsNodeGetBool(moduleData->moduleNode, "doNormInputImages");
	state->lowPassNumber = sshsNodeGetInt(moduleData->moduleNode, "lowPassNumers");

}

static void caerCaffeWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {

	caerFrameEventPacketConst frameIn =
			(caerFrameEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, FRAME_EVENT);

	caffewrapperState state = moduleData->moduleState;

	caerCaffeWrapperUpdateConfigs(moduleData);

	if(frameIn != NULL){
		MyCaffe_file_set(state->cpp_class, frameIn, state->detThreshold, state->doPrintOutputs,
			state->doShowActivations, state->doNormInputImages);
	}

}
