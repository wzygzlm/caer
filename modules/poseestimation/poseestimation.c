#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "poseestimation_settings.h"
#include "poseestimation_wrapper.h"

#include <limits.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

struct PoseEstimationState_struct {
	struct PoseEstimationSettings_struct settings; // Struct containing all settings (shared)
	struct PoseEstimation *cpp_class; // Pointer to cpp_class_object
	uint64_t lastFrameTimestamp;
	size_t lastFoundPoints;
	bool calibrationLoaded;
};

typedef struct PoseEstimationState_struct *PoseEstimationState;

static bool caerPoseEstimationInit(caerModuleData moduleData);
static void caerPoseEstimationRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerPoseEstimationExit(caerModuleData moduleData);
static void updateSettings(caerModuleData moduleData);

static const struct caer_module_functions PoseEstimationFunctions = { .moduleInit = &caerPoseEstimationInit,
	.moduleRun = &caerPoseEstimationRun, .moduleConfig = NULL, .moduleExit =
		&caerPoseEstimationExit };

static const struct caer_event_stream_in PoseEstimationInputs[] = { { .type = FRAME_EVENT, .number = 1, .readOnly = false } };

static const struct caer_module_info PoseEstimationInfo = { .version = 1, .name = "PoseEstimation", .type =
	CAER_MODULE_PROCESSOR, .memSize = sizeof(struct PoseEstimationState_struct), .functions =
	&PoseEstimationFunctions, .inputStreams = PoseEstimationInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(
		PoseEstimationInputs), .outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&PoseEstimationInfo);
}

static bool caerPoseEstimationInit(caerModuleData moduleData) {
	PoseEstimationState state = moduleData->moduleState;

	// Create config settings.
	sshsNodeCreateBool(moduleData->moduleNode, "detectMarkers", false, SSHS_FLAGS_NORMAL); // Do calibration using live images
	sshsNodeCreateString(moduleData->moduleNode, "saveFileName", "camera_calib.xml", 1, PATH_MAX, SSHS_FLAGS_NORMAL); // The name of the file where to write the calculated calibration settings
	sshsNodeCreateString(moduleData->moduleNode, "loadFileName", "camera_calib.xml", 1, PATH_MAX, SSHS_FLAGS_NORMAL); // The name of the file from which to load the calibration
	sshsNodeCreateInt(moduleData->moduleNode, "captureDelay", 500000, 0, 10 * 1000 * 1000, SSHS_FLAGS_NORMAL);

	// Update all settings.
	updateSettings(moduleData);

	// Initialize C++ class for OpenCV integration.
	state->cpp_class = poseestimation_init(&state->settings);
	if (state->cpp_class == NULL) {
		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);
	//not loaded at the init
	state->calibrationLoaded = false;

	return (true);
}

static void updateSettings(caerModuleData moduleData) {
	PoseEstimationState state = moduleData->moduleState;

	state->settings.detectMarkers = sshsNodeGetBool(moduleData->moduleNode, "detectMarkers");
	state->settings.saveFileName = sshsNodeGetString(moduleData->moduleNode, "saveFileName");
	state->settings.loadFileName = sshsNodeGetString(moduleData->moduleNode, "loadFileName");
}

static void caerPoseEstimationExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	PoseEstimationState state = moduleData->moduleState;

	//poseestimation_destroy(state->cpp_class);

	free(state->settings.saveFileName);
	free(state->settings.loadFileName);
}

static void caerPoseEstimationRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	// Interpret variable arguments (same as above in main function).
	caerFrameEventPacket frame = (caerFrameEventPacket) caerEventPacketContainerFindEventPacketByType(in, FRAME_EVENT);

	PoseEstimationState state = moduleData->moduleState;

	// At this point we always try to load the calibration settings for undistortion.
	// Maybe they just got created or exist from a previous run.
	if (!state->calibrationLoaded) {
		state->calibrationLoaded = poseestimation_loadCalibrationFile(state->cpp_class, &state->settings);
	}

	// Marker pose estimation is done only using frames.
	if (state->settings.detectMarkers && frame != NULL) {
		CAER_FRAME_ITERATOR_VALID_START(frame)
			// Only work on new frames if enough time has passed between this and the last used one.
			uint64_t currTimestamp = U64T(caerFrameEventGetTSStartOfFrame64(caerFrameIteratorElement, frame));

			// If enough time has passed, try to add a new point set.
			if ((currTimestamp - state->lastFrameTimestamp) >= state->settings.captureDelay) {
				state->lastFrameTimestamp = currTimestamp;

				bool foundPoint = poseestimation_findMarkers(state->cpp_class, caerFrameIteratorElement);
				caerLog(CAER_LOG_WARNING, moduleData->moduleSubSystemString,
					"Searching for markers in the aruco set, result = %d.", foundPoint);
			}
		CAER_FRAME_ITERATOR_VALID_END
	}

	// update settings
	updateSettings(moduleData);
}
