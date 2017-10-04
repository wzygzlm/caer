#include "main.h"
#include "opticflow_wrapper.h"
#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/events/frame.h>


struct OpticFlowState_struct {
	struct OpticFlowSettings_struct settings; // Struct containing all settings (shared)
	struct OpticFlow *cpp_class; // Pointer to cpp_class_object
	int sizeX;
	int sizeY;
};

typedef struct OpticFlowState_struct *OpticFlowState;

static bool caerOpticFlowInit(caerModuleData moduleData);
static void caerOpticFlowRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerOpticFlowConfig(caerModuleData moduleData);
static void caerOpticFlowExit(caerModuleData moduleData);
static void updateSettings(caerModuleData moduleData);

static struct caer_module_functions caerOpticFlowFunctions = { .moduleInit = &caerOpticFlowInit, .moduleRun =
	&caerOpticFlowRun, .moduleConfig = &caerOpticFlowConfig, .moduleExit = &caerOpticFlowExit };


static const struct caer_event_stream_in moduleInputs[] = { { .type = FRAME_EVENT, .number = 1, .readOnly = true } };

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "Optic Flow",
	.description = "Optic Flow on accumulated event stream",
	.type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct OpticFlowState_struct),
	.functions = &caerOpticFlowFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = moduleOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs)
};

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}


static bool caerOpticFlowInit(caerModuleData moduleData) {
	OpticFlowState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	// Wait for source size information to be available.
	// Allocate map using info from sourceInfo.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfo, "frameSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfo, "frameSizeY");

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", sizeX, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output frame width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", sizeY, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output frame height.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", sizeX, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", sizeY, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output data height.");


	// Create config settings.
	sshsNodeCreateBool(moduleData->moduleNode, "doOpticFlow", true, SSHS_FLAGS_NORMAL, "Run optic flow estimation");

	// Update all settings.
	updateSettings(moduleData);

	// Initialize C++ class for OpenCV integration.
	state->cpp_class = OpticFlow_init(&state->settings);
	if (state->cpp_class == NULL) {
		return (false);
	}


	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	return (true);
}

static void updateSettings(caerModuleData moduleData) {
	OpticFlowState state = moduleData->moduleState;

	// Get current config settings.
	state->settings.doOpticFlow = sshsNodeGetBool(moduleData->moduleNode, "doOpticFlow");
}

static void caerOpticFlowExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	OpticFlowState state = moduleData->moduleState;

	OpticFlow_destroy(state->cpp_class);

}

static void caerOpticFlowConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	OpticFlowState state = moduleData->moduleState;

	// Reload all local settings.
	updateSettings(moduleData);

	// Update the C++ internal state, based on new settings.
	OpticFlow_updateSettings(state->cpp_class);

}

static void caerOpticFlowRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	// Interpret variable arguments (same as above in main function).
	caerFrameEventPacketConst frameInput =
			(caerFrameEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, FRAME_EVENT);

	if (frameInput == NULL) {
		return;
	}

	OpticFlowState state = moduleData->moduleState;


	int32_t packetNum = caerEventPacketHeaderGetEventNumber((caerEventPacketHeader) frameInput);
	if(packetNum == 0){
		return;
	}

	caerFrameEvent eventS = caerFrameEventPacketGetEventConst(frameInput, 0);
	int sizeX = caerFrameEventGetLengthX(eventS);
	int sizeY = caerFrameEventGetLengthY(eventS);

	// Generate image.
	caerFrameEventPacket frameOut = NULL;

	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
		return; // Error.
	}

	// everything that is in the out packet container will be automatically be free after main loop
	frameOut = caerFrameEventPacketAllocate(1, moduleData->moduleID,
		caerEventPacketHeaderGetEventTSOverflow(&frameInput->packetHeader), I32T(sizeX),
		I32T(sizeY), RGB);
	if (frameOut == NULL) {
		return; // Error.
	}
	else {
		// Add output packet to packet container.
		caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) frameOut);
	}
	caerFrameEvent single_frame = caerFrameEventPacketGetEvent(frameOut, 0);

	//add info to the frame
	caerFrameEventSetLengthXLengthYChannelNumber(single_frame, sizeX, sizeY, RGB, frameOut); // to do remove hard coded size
	caerFrameEvent single_frame_in = caerFrameEventPacketGetEvent(frameInput, 0);

	CAER_FRAME_ITERATOR_VALID_START(frameInput)

		int sizeX = caerFrameEventGetLengthX(caerFrameIteratorElement);
		int sizeY = caerFrameEventGetLengthY(caerFrameIteratorElement);
		OpticFlow_doOpticFlow(state->cpp_class, &single_frame, &single_frame_in, sizeX, sizeY);

	CAER_FRAME_ITERATOR_VALID_END

	// validate frame
	caerFrameEventValidate(single_frame, frameOut);


}
