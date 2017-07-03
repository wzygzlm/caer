#include "poseestimation.hpp"
#include "poseestimation_wrapper.h"

PoseEstimation *poseestimation_init(PoseEstimationSettings settings) {
	try {
		return (new PoseEstimation(settings));
	}
	catch (const std::exception& ex) {
		caerLog(CAER_LOG_ERROR, "PoseEstimation()", "Failed with C++ exception: %s", ex.what());
		return (NULL);
	}
}

void poseestimation_destroy(PoseEstimation *calibClass) {
	try {
		delete calibClass;
	}
	catch (const std::exception& ex) {
		caerLog(CAER_LOG_ERROR, "PoseEstimation_destroy()", "Failed with C++ exception: %s", ex.what());
	}
}

void poseestimation_updateSettings(PoseEstimation *calibClass) {

}

bool poseestimation_findMarkers(PoseEstimation *calibClass, caerFrameEvent frame) {
	try {
		return (calibClass->findMarkers(frame));
	}
	catch (const std::exception& ex) {
		caerLog(CAER_LOG_ERROR, "PoseEstimation_findMarkers()", "Failed with C++ exception: %s", ex.what());
		return (false);
	}
}

bool poseestimation_loadCalibrationFile(PoseEstimation *calibClass, PoseEstimationSettings settings) {
	try {
		return (calibClass->loadCalibrationFile(settings));
	}
	catch (const std::exception& ex) {
		caerLog(CAER_LOG_ERROR, "PoseEstimation_loadCalibrationFile()", "Failed with C++ exception: %s", ex.what());
		return (false);
	}
}

