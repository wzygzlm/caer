#ifndef STEREOMATCHING_SETTINGS_H_
#define STEREOMATCHING_SETTINGS_H_

enum StereoMatchingAlg {  STEREO_SGBM=1, STEREO_HH=2, STEREO_3WAY=4 };

struct StereoMatchingSettings_struct {
	int alg;
	int doMatching;
	int captureDelay;
	char * loadFileName_extrinsic;
	char * loadFileName_intrinsic;
	enum StereoMatchingAlg stereoMatchingAlg;
	int minDisparity;
	int numDisparities;
	int blockSize;
	int PP1;
	int PP2;
	int disp12MaxDiff;
	int preFilterCap;
	int uniquenessRatio;
	int speckleWindowSize;
	int speckleRange;
};

typedef struct StereoMatchingSettings_struct *StereoMatchingSettings;


#endif /* STEREOMATCHING_SETTINGS_H_ */
