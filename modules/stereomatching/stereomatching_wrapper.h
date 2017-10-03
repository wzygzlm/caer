#ifndef CALIBRATION_WRAPPER_H_
#define CALIBRATION_WRAPPER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "matching_settings.h"
#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

typedef struct StereoMatching StereoMatching;

StereoMatching *StereoMatching_init(StereoMatchingSettings settings);
void StereoMatching_destroy(StereoMatching *matchingClass);
void StereoMatching_updateSettings(StereoMatching *matchingClass);
int StereoMatching_loadCalibrationFile(StereoMatching *matchingClass,
		StereoMatchingSettings settings);
int StereoMatching_stereoMatch(StereoMatching *matchingClass, StereoMatchingSettings setting,  caerFrameEvent vec1, caerFrameEvent vec2);

#ifdef __cplusplus
}
#endif

#endif /* STEREOMATCHING_WRAPPER_H_ */
