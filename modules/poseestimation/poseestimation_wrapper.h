#ifndef CALIBRATION_WRAPPER_H_
#define CALIBRATION_WRAPPER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "poseestimation_settings.h"

#include <libcaer/events/frame.h>

typedef struct PoseEstimation PoseEstimation;

PoseEstimation *poseestimation_init(PoseEstimationSettings settings);
void poseestimation_destroy(PoseEstimation *calibClass);
void poseestimation_updateSettings(PoseEstimation *calibClass);
bool poseestimation_findMarkers(PoseEstimation *calibClass, caerFrameEvent frame);
bool poseestimation_loadCalibrationFile(PoseEstimation *calibClass, PoseEstimationSettings settings);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_WRAPPER_H_ */
