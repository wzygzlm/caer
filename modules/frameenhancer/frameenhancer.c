#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/frame.h>
#include <libcaer/frame_utils.h>

struct FrameEnhancer_state {
	bool doDemosaic;
	enum caer_frame_utils_demosaic_types demosaicType;
	bool doContrast;
	enum caer_frame_utils_contrast_types contrastType;
};

typedef struct FrameEnhancer_state *FrameEnhancerState;

static bool caerFrameEnhancerInit(caerModuleData moduleData);
static void caerFrameEnhancerRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerFrameEnhancerConfig(caerModuleData moduleData);
static void caerFrameEnhancerExit(caerModuleData moduleData);

static const struct caer_module_functions FrameEnhancerFunctions = { .moduleInit = &caerFrameEnhancerInit, .moduleRun =
	&caerFrameEnhancerRun, .moduleConfig = &caerFrameEnhancerConfig, .moduleExit = &caerFrameEnhancerExit };

static const struct caer_event_stream_in FrameEnhancerInputs[] = {
	{ .type = FRAME_EVENT, .number = 1, .readOnly = true } };
// The output frame here is a _different_ frame than the above input!
static const struct caer_event_stream_out FrameEnhancerOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info FrameEnhancerInfo = { .version = 1, .name = "FrameEnhancer", .description =
	"Applies contrast enhancement techniques to frames, or interpolates colors to get an RGB frame (demoisaicing).",
	.type = CAER_MODULE_PROCESSOR, .memSize = sizeof(struct FrameEnhancer_state), .functions = &FrameEnhancerFunctions,
	.inputStreams = FrameEnhancerInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(FrameEnhancerInputs),
	.outputStreams = FrameEnhancerOutputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(FrameEnhancerOutputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&FrameEnhancerInfo);
}

static bool caerFrameEnhancerInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateBool(moduleData->moduleNode, "doDemosaic", false, SSHS_FLAGS_NORMAL,
		"Do demosaicing (color interpolation) on frame.");
	sshsNodeCreateBool(moduleData->moduleNode, "doContrast", false, SSHS_FLAGS_NORMAL,
		"Do contrast enhancement on frame.");

#if defined(LIBCAER_HAVE_OPENCV) && LIBCAER_HAVE_OPENCV == 1
	sshsNodeCreateString(moduleData->moduleNode, "demosaicType", "opencv_edge_aware", 8, 17, SSHS_FLAGS_NORMAL,
		"Demoisaicing (color interpolation) algorithm to apply.");
	sshsNodeCreateAttributeListOptions(moduleData->moduleNode, "demosaicType", SSHS_STRING,
		"opencv_edge_aware,opencv_normal,standard", false);
	sshsNodeCreateString(moduleData->moduleNode, "contrastType", "opencv_normalization", 8, 29, SSHS_FLAGS_NORMAL,
		"Contrast enhancement algorithm to apply.");
	sshsNodeCreateAttributeListOptions(moduleData->moduleNode, "contrastType", SSHS_STRING,
		"opencv_normalization,opencv_histogram_equalization,opencv_clahe,standard", false);
#else
	// Only standard algorithms are available here, so we force those and make it read-only.
	sshsNodeRemoveAttribute(moduleData->moduleNode, "demosaicType", SSHS_STRING);
	sshsNodeCreateString(moduleData->moduleNode, "demosaicType", "standard", 8, 8,
		SSHS_FLAGS_READ_ONLY, "Demoisaicing (color interpolation) algorithm to apply.");
	sshsNodeRemoveAttribute(moduleData->moduleNode, "contrastType", SSHS_STRING);
	sshsNodeCreateString(moduleData->moduleNode, "contrastType", "standard", 8, 8,
		SSHS_FLAGS_READ_ONLY, "Contrast enhancement algorithm to apply.");
#endif

	sshsNode sourceInfoSource = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfoSource == NULL) {
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoSource, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoSource, "dataSizeY");

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output frame width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output frame height.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output data height.");

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
		enhancedFrame = caerFrameUtilsDemosaic(frame, state->demosaicType);
#else
		enhancedFrame = caerFrameUtilsDemosaic(frame, DEMOSAIC_STANDARD);
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
		caerFrameUtilsContrast(enhancedFrame, state->contrastType);
#else
		caerFrameUtilsContrast(enhancedFrame, CONTRAST_STANDARD);
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

	state->doContrast = sshsNodeGetBool(moduleData->moduleNode, "doContrast");

#if defined(LIBCAER_HAVE_OPENCV) && LIBCAER_HAVE_OPENCV == 1
	char *demosaicType = sshsNodeGetString(moduleData->moduleNode, "demosaicType");

	if (caerStrEquals(demosaicType, "opencv_normal")) {
		state->demosaicType = DEMOSAIC_OPENCV_NORMAL;
	}
	else if (caerStrEquals(demosaicType, "opencv_edge_aware")) {
		state->demosaicType = DEMOSAIC_OPENCV_EDGE_AWARE;
	}
	else {
		// Standard, non-OpenCV method.
		state->demosaicType = DEMOSAIC_STANDARD;
	}

	free(demosaicType);

	char *contrastType = sshsNodeGetString(moduleData->moduleNode, "contrastType");

	if (caerStrEquals(contrastType, "opencv_normalization")) {
		state->contrastType = CONTRAST_OPENCV_NORMALIZATION;
	}
	else if (caerStrEquals(contrastType, "opencv_histogram_equalization")) {
		state->contrastType = CONTRAST_OPENCV_HISTOGRAM_EQUALIZATION;
	}
	else if (caerStrEquals(contrastType, "opencv_clahe")) {
		state->contrastType = CONTRAST_OPENCV_CLAHE;
	}
	else {
		// Standard, non-OpenCV method.
		state->contrastType = CONTRAST_STANDARD;
	}

	free(contrastType);
#endif
}

static void caerFrameEnhancerExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeClearSubTree(sourceInfoNode, true);
}
