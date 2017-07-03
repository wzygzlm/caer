#ifndef POSEESTIMATION_SETTINGS_H_
#define POSEESTIMATION_SETTINGS_H_


struct PoseEstimationSettings_struct {
	bool detectMarkers;
	char *saveFileName;
	uint32_t captureDelay;
	char *loadFileName;
};

typedef struct PoseEstimationSettings_struct *PoseEstimationSettings;


#endif /* POSEESTIMATION_SETTINGS_H_ */
