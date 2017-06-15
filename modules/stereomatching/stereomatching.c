#include "stereomatching.h"
#include "matching_settings.h"
#include "stereomatching_wrapper.h"
#include "base/mainloop.h"
#include "base/module.h"

struct StereoMatchingState_struct {
	struct StereoMatchingSettings_struct settings; // Struct containing all settings (shared)
	struct StereoMatching *cpp_class; 			  // Pointer to cpp_class_object
	uint64_t lastFrameTimestamp_cam0;
	uint64_t lastFrameTimestamp_cam1;
	uint32_t points_found;
	uint32_t last_points_found;
	size_t lastFoundPoints;
	bool calibrationLoaded;
	caerFrameEventPacket cam0;
	caerFrameEventPacket cam1;
};

typedef struct StereoMatchingState_struct *StereoMatchingState;

static bool caerStereoMatchingInit(caerModuleData moduleData);
static void caerStereoMatchingRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerStereoMatchingConfig(caerModuleData moduleData);
static void caerStereoMatchingExit(caerModuleData moduleData);
static void updateSettings(caerModuleData moduleData);

static struct caer_module_functions caerStereoMatchingFunctions = { .moduleInit = &caerStereoMatchingInit,
	.moduleRun = &caerStereoMatchingRun, .moduleConfig = &caerStereoMatchingConfig, .moduleExit =
		&caerStereoMatchingExit };

void caerStereoMatching(uint16_t moduleID, caerFrameEventPacket frame_0, caerFrameEventPacket frame_1) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "StereoMatching", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}

	caerModuleSM(&caerStereoMatchingFunctions, moduleData, sizeof(struct StereoMatchingState_struct), 2, frame_0,
		frame_1);
}

static bool caerStereoMatchingInit(caerModuleData moduleData) {
	StereoMatchingState state = moduleData->moduleState;

	// Create config settings.
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "doMatching", false); // Do calibration using live images
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "captureDelay", 2000);
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "loadFileName_extrinsic", "extrinsics.xml"); // The name of the file from which to load the calibration
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "loadFileName_intrinsic", "intrinsics.xml"); // The name of the file from which to load the calibration

	sshsNodePutIntIfAbsent(moduleData->moduleNode, "minDisparity", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "numDisparities", 16);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "blockSize", 3);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "PP1", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "PP2", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "disp12MaxDiff", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "preFilterCap", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "uniquenessRatio", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "speckleWindowSize", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "speckleRange", 0);
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "stereoMatchingAlg", "STEREO_SGBM"); //  STEREO_SGBM=1, STEREO_HH=2,  STEREO_3WAY=4
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "stereoMatchingAlgListOptions", "STEREO_SGBM,STEREO_HH,STEREO_3WAY");

	// Update all settings.
	updateSettings(moduleData);

	// Initialize C++ class for OpenCV integration.
	state->cpp_class = StereoMatching_init(&state->settings);
	if (state->cpp_class == NULL) {
		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	return (true);
}

static void updateSettings(caerModuleData moduleData) {
	StereoMatchingState state = moduleData->moduleState;

	state->settings.doMatching = sshsNodeGetBool(moduleData->moduleNode, "doMatching");
	state->settings.captureDelay = sshsNodeGetInt(moduleData->moduleNode, "captureDelay");
	state->settings.loadFileName_extrinsic = sshsNodeGetString(moduleData->moduleNode, "loadFileName_extrinsic");
	state->settings.loadFileName_intrinsic = sshsNodeGetString(moduleData->moduleNode, "loadFileName_intrinsic");

	state->settings.minDisparity = sshsNodeGetInt(moduleData->moduleNode, "minDisparity");
	state->settings.numDisparities = sshsNodeGetInt(moduleData->moduleNode, "numDisparities");
	state->settings.blockSize = sshsNodeGetInt(moduleData->moduleNode, "blockSize");
	state->settings.PP1 = sshsNodeGetInt(moduleData->moduleNode, "PP1");
	state->settings.PP2 = sshsNodeGetInt(moduleData->moduleNode, "PP2");
	state->settings.disp12MaxDiff = sshsNodeGetInt(moduleData->moduleNode, "disp12MaxDiff");
	state->settings.preFilterCap = sshsNodeGetInt(moduleData->moduleNode, "preFilterCap");
	state->settings.uniquenessRatio = sshsNodeGetInt(moduleData->moduleNode, "uniquenessRatio");
	state->settings.speckleWindowSize = sshsNodeGetInt(moduleData->moduleNode, "speckleWindowSize");
	state->settings.speckleRange = sshsNodeGetInt(moduleData->moduleNode, "speckleRange");

	// Parse stereoMatchingAlg string.
	char *calibPattern = sshsNodeGetString(moduleData->moduleNode, "stereoMatchingAlg");
	if (caerStrEquals(calibPattern, "STEREO_SGBM")) {
		state->settings.stereoMatchingAlg = STEREO_SGBM;
	}
	else if (caerStrEquals(calibPattern, "STEREO_HH")) {
		state->settings.stereoMatchingAlg = STEREO_HH;
	}
	else if (caerStrEquals(calibPattern, "STEREO_3WAY")) {
		state->settings.stereoMatchingAlg = STEREO_3WAY;
	}
	else {
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
			"Invalid stereoMatchingAlg defined. Select one of: STEREO_SGBM, STEREO_HH, STEREO_3WAY. Defaulting to STEREO_SBGM.");
		state->settings.stereoMatchingAlg = STEREO_SGBM;
	}
	free(calibPattern);

}

static void caerStereoMatchingConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	StereoMatchingState state = moduleData->moduleState;

}

static void caerStereoMatchingExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	StereoMatchingState state = moduleData->moduleState;

}

static void caerStereoMatchingRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).
	caerFrameEventPacket frame_0 = va_arg(args, caerFrameEventPacket);
	caerFrameEventPacket frame_1 = va_arg(args, caerFrameEventPacket);

	StereoMatchingState state = moduleData->moduleState;

	// At this point we always try to load the calibration settings for undistorsion.
	// Maybe they just got created or exist from a previous run.
	if (!state->calibrationLoaded) {
		state->calibrationLoaded = StereoMatching_loadCalibrationFile(state->cpp_class, &state->settings);
	}

	if (frame_0 != NULL && frame_1 == NULL) {
		free(state->cam0);
		state->cam0 = caerEventPacketCopy(frame_0);

		if (state->cam1 != NULL) {
			frame_1 = state->cam1;
		}
	}

	if (frame_1 != NULL && frame_0 == NULL) {
		free(state->cam1);
		state->cam1 = caerEventPacketCopy(frame_1);

		if (state->cam0 != NULL) {
			frame_0 = state->cam0;
		}
	}

	// Stereo Camera calibration is done only using frames.
	if (state->settings.doMatching && frame_0 != NULL && frame_1 != NULL) {

		//caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Looking for calibration patterns...");

		bool frame_0_pattern = false;
		uint64_t frame_0_ts = NULL;
		bool frame_1_pattern = false;
		uint64_t frame_1_ts = NULL;
		void * foundPoint_cam1 = NULL;
		void * foundPoint_cam0 = NULL;


		// get last valid frame in packet for both cameras
		caerFrameEventPacket currFramePacket_cam0 = (caerFrameEventPacket) frame_0;
		caerFrameEvent currFrameEvent_cam0;
		bool have_frame_0 = false;

		for (int32_t i = caerEventPacketHeaderGetEventNumber(&currFramePacket_cam0->packetHeader) - 1; i >= 0; i--) {
			currFrameEvent_cam0 = caerFrameEventPacketGetEvent(currFramePacket_cam0, i);
			if (caerFrameEventIsValid(currFrameEvent_cam0)) {
				//currFrameEvent_cam0
				have_frame_0 = true;
				break;
			}
		}

		caerFrameEventPacket currFramePacket_cam1 = (caerFrameEventPacket) frame_1;
		caerFrameEvent currFrameEvent_cam1;
		bool have_frame_1 = false;

		for (int32_t i = caerEventPacketHeaderGetEventNumber(&currFramePacket_cam1->packetHeader) - 1; i >= 0; i--) {
			currFrameEvent_cam1 = caerFrameEventPacketGetEvent(currFramePacket_cam1, i);
			if (caerFrameEventIsValid(currFrameEvent_cam1)) {
				//currFrameEvent_cam1
				have_frame_1 = true;
				break;
			}
		}

		if(have_frame_1 && have_frame_0){
			//we got frames proceed with stereo matching
			//caerLog(CAER_LOG_ERROR, __func__, "Doing Stereo Matching");
			StereoMatching_stereoMatch(state->cpp_class,  &state->settings, currFrameEvent_cam0, currFrameEvent_cam1);
		}

	}

	// update settings
	updateSettings(moduleData);

}
