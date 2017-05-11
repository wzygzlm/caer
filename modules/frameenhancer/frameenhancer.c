#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/frame.h>
#include <libcaer/frame_utils.h>

struct FrameEnhancer_state {
	bool doDemosaic;
	int demosaicType;
	bool doContrast;
	int contrastType;
};

typedef struct FrameEnhancer_state *FrameEnhancerState;

static bool caerFrameEnhancerInit(caerModuleData moduleData);
static void caerFrameEnhancerRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerFrameEnhancerConfig(caerModuleData moduleData);
static void caerFrameEnhancerExit(caerModuleData moduleData);

static const struct caer_module_functions FrameEnhancerFunctions = { .moduleInit = &caerFrameEnhancerInit, .moduleRun =
	&caerFrameEnhancerRun, .moduleConfig = &caerFrameEnhancerConfig, .moduleExit = &caerFrameEnhancerExit };

static const struct caer_event_stream_in FrameEnhancerInputs[] = { { .type = FRAME_EVENT, .number = 1, .readOnly = true } };
// The output frame here is a _different_ frame than the above input!
static const struct caer_event_stream_out FrameEnhancerOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info FrameEnhancerInfo = { .version = 1, .name = "FrameEnhancer", .type =
	CAER_MODULE_PROCESSOR, .memSize = sizeof(struct FrameEnhancer_state), .functions = &FrameEnhancerFunctions,
	.inputStreams = FrameEnhancerInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(FrameEnhancerInputs),
	.outputStreams = FrameEnhancerOutputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(FrameEnhancerOutputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&FrameEnhancerInfo);
}

static bool caerFrameEnhancerInit(caerModuleData moduleData) {
	sshsNodeCreateBool(moduleData->moduleNode, "doDemosaic", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(moduleData->moduleNode, "doContrast", false, SSHS_FLAGS_NORMAL);

#if defined(LIBCAER_HAVE_OPENCV) && LIBCAER_HAVE_OPENCV == 1
	sshsNodeCreateString(moduleData->moduleNode, "demosaicType", "opencv_edge_aware", 8, 17, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(moduleData->moduleNode, "contrastType", "opencv_normalization", 8, 29, SSHS_FLAGS_NORMAL);
#else
	sshsNodeCreateString(moduleData->moduleNode, "demosaicType", "standard", 8, 8, SSHS_FLAGS_READ_ONLY);
	sshsNodeCreateString(moduleData->moduleNode, "contrastType", "standard", 8, 8, SSHS_FLAGS_READ_ONLY);
#endif

	// Initialize configuration.
	caerFrameEnhancerConfig(moduleData);

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerFrameEnhancerRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerFrameEventPacketConst frame = (caerFrameEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in,
		FRAME_EVENT);

	// Only process packets with content.
	if (frame == NULL) {
		return;
	}

	FrameEnhancerState state = moduleData->moduleState;
	caerFrameEventPacket enhancedFrame = NULL;

	if (state->doDemosaic) {
#if defined(LIBCAER_HAVE_OPENCV) && LIBCAER_HAVE_OPENCV == 1
		switch (state->demosaicType) {
			case 0:
				enhancedFrame = caerFrameUtilsDemosaic(frame);
				break;

			case 1:
				enhancedFrame = caerFrameUtilsOpenCVDemosaic(frame, DEMOSAIC_NORMAL);
				break;

			case 2:
				enhancedFrame = caerFrameUtilsOpenCVDemosaic(frame, DEMOSAIC_EDGE_AWARE);
				break;
		}
#else
		enhancedFrame = caerFrameUtilsDemosaic(frame);
#endif
	}

	if (state->doContrast) {
		// If enhancedFrame doesn't exist yet, make a copy of frame, since
		// the demosaic operation didn't do it for us.
		if (enhancedFrame == NULL) {
			enhancedFrame = (caerFrameEventPacket) caerEventPacketCopyOnlyValidEvents(
				(caerEventPacketHeaderConst) frame);
			if (enhancedFrame == NULL) {
				return;
			}
		}

#if defined(LIBCAER_HAVE_OPENCV) && LIBCAER_HAVE_OPENCV == 1
		switch (state->contrastType) {
			case 0:
				caerFrameUtilsContrast(enhancedFrame);
				break;

			case 1:
				caerFrameUtilsOpenCVContrast(enhancedFrame, CONTRAST_NORMALIZATION);
				break;

			case 2:
				caerFrameUtilsOpenCVContrast(enhancedFrame, CONTRAST_HISTOGRAM_EQUALIZATION);
				break;

			case 3:
				caerFrameUtilsOpenCVContrast(enhancedFrame, CONTRAST_CLAHE);
				break;
		}
#else
		caerFrameUtilsContrast(enhancedFrame);
#endif
	}

	// If something did happen, make a packet container and return the result.
	// Also remember to put this new container up for freeing at loop end.
	if (enhancedFrame != NULL) {
		*out = caerEventPacketContainerAllocate(1);
		if (*out == NULL) {
			free(enhancedFrame);
			return;
		}

		caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) enhancedFrame);

		// Source ID must be this module!
		caerEventPacketHeaderSetEventSource((caerEventPacketHeader) enhancedFrame, moduleData->moduleID);
	}
}

static void caerFrameEnhancerConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	FrameEnhancerState state = moduleData->moduleState;

	state->doDemosaic = sshsNodeGetBool(moduleData->moduleNode, "doDemosaic");

	char *demosaicType = sshsNodeGetString(moduleData->moduleNode, "demosaicType");

	if (caerStrEquals(demosaicType, "opencv_normal")) {
		state->demosaicType = 1;
	}
	else if (caerStrEquals(demosaicType, "opencv_edge_aware")) {
		state->demosaicType = 2;
	}
	else {
		// Standard, non-OpenCV method.
		state->demosaicType = 0;
	}

	free(demosaicType);

	state->doContrast = sshsNodeGetBool(moduleData->moduleNode, "doContrast");

	char *contrastType = sshsNodeGetString(moduleData->moduleNode, "contrastType");

	if (caerStrEquals(contrastType, "opencv_normalization")) {
		state->contrastType = 1;
	}
	else if (caerStrEquals(contrastType, "opencv_histogram_equalization")) {
		state->contrastType = 2;
	}
	else if (caerStrEquals(contrastType, "opencv_clahe")) {
		state->contrastType = 3;
	}
	else {
		// Standard, non-OpenCV method.
		state->contrastType = 0;
	}

	free(contrastType);
}

static void caerFrameEnhancerExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);
}
