#include "stereomatching.hpp"
#include <fstream>
#include <iostream>
#include <opencv2/cudastereo.hpp>

StereoMatching::StereoMatching(StereoMatchingSettings settings){

	updateSettings(this->settings);

	cv::namedWindow( "Matching Debug1", WINDOW_AUTOSIZE );
	cv::namedWindow( "Matching Debug2", WINDOW_AUTOSIZE );

	//updateSettings(settings);
}

int StereoMatching::stereoMatch(StereoMatchingSettings settings, caerFrameEvent vec1, caerFrameEvent vec2) {
	this->settings = settings;

	// Initialize OpenCV Mat based on caerFrameEvent data directly (no image copy).
	Size frameSize_cam0(caerFrameEventGetLengthX(vec1), caerFrameEventGetLengthY(vec1));
	Mat Image_cam0(frameSize_cam0, CV_16UC(caerFrameEventGetChannelNumber(vec1)), caerFrameEventGetPixelArrayUnsafe(vec1));

	Size frameSize_cam1(caerFrameEventGetLengthX(vec2), caerFrameEventGetLengthY(vec2));
	Mat Image_cam1(frameSize_cam1, CV_16UC(caerFrameEventGetChannelNumber(vec2)), caerFrameEventGetPixelArrayUnsafe(vec2));

	/*Rect roi1, roi2;

    cv::stereoRectify( M1, D1, M2, D2, frameSize_cam0, R, T, R1, R2, P1, P2, Q, CALIB_ZERO_DISPARITY, -1, frameSize_cam0, &roi1, &roi2);

    Mat map11, map12, map21, map22;
    cv::initUndistortRectifyMap(M1, D1, R1, P1, frameSize_cam0, CV_16SC2, map11, map12);
    cv::initUndistortRectifyMap(M2, D2, R2, P2, frameSize_cam1, CV_16SC2, map21, map22);

    Mat img1r, img2r;
	cv::remap(Image_cam0, img1r, map11, map12, INTER_LINEAR);
	cv::remap(Image_cam1, img2r, map21, map22, INTER_LINEAR);

	Image_cam0 = img1r;
	Image_cam1 = img2r;*/

	// stereo disparity map
    //-- And create the image in which we will save our disparities
    Mat imgDisparity16S = Mat( Image_cam1.rows, Image_cam1.cols, CV_16S );
    Mat imgDisparity8U = Mat( Image_cam1.rows, Image_cam1.cols, CV_8UC1 );

    //-- 2. Call the constructor for StereoBM
    int ndisparities = 16*2;    //< Range of disparity
    int SADWindowSize = 21; 	//< Size of the block window. Must be odd

    Ptr<StereoSGBM> sbm;
    // ndisparities, SADWindowSize
    switch(settings->stereoMatchingAlg) {
    		case STEREO_SGBM:
    		{
    		    sbm  = StereoSGBM::create(
    		    	settings->minDisparity, settings->numDisparities, settings->blockSize,  settings->PP1,  settings->PP2,
    				settings->disp12MaxDiff,  settings->preFilterCap, settings->uniquenessRatio, settings->speckleWindowSize,
    				settings->speckleRange, StereoSGBM::MODE_SGBM);
    		    break;
    		}
    		case STEREO_HH:
    		{
    		    sbm  = StereoSGBM::create(
    		    	settings->minDisparity, settings->numDisparities, settings->blockSize,  settings->PP1,  settings->PP2,
    				settings->disp12MaxDiff,  settings->preFilterCap, settings->uniquenessRatio, settings->speckleWindowSize,
    				settings->speckleRange, StereoSGBM::MODE_HH);
    		    break;
    		}
    		case STEREO_3WAY:
    		{
    		    sbm  = StereoSGBM::create(
    		    	settings->minDisparity, settings->numDisparities, settings->blockSize,  settings->PP1,  settings->PP2,
    				settings->disp12MaxDiff,  settings->preFilterCap, settings->uniquenessRatio, settings->speckleWindowSize,
    				settings->speckleRange, StereoSGBM::MODE_SGBM_3WAY);
    		    break;
    		}
    }


    //-- 3. Calculate the disparity image
    Mat Image_cam0_gs = Mat( Image_cam1.rows, Image_cam1.cols, CV_8UC1 );
    Mat Image_cam1_gs = Mat( Image_cam1.rows, Image_cam1.cols, CV_8UC1 );

    Image_cam0.convertTo( Image_cam0_gs, COLOR_GRAY2RGB,  1.0/255.0);
    Image_cam1.convertTo( Image_cam1_gs, COLOR_GRAY2RGB,  1.0/255.0);

    Mat res_cam0, res_cam1;
    cv::cvtColor(Image_cam0_gs, res_cam0, CV_BGRA2GRAY);
    cv::cvtColor(Image_cam1_gs, res_cam1, CV_BGRA2GRAY);

    sbm->compute( res_cam0, res_cam1, imgDisparity16S );

    //-- Check its extreme values
    double minVal; double maxVal;
    minMaxLoc( imgDisparity16S, &minVal, &maxVal );

    //-- 4. Display it as a CV_8UC1 image
    //imgDisparity16S.convertTo( imgDisparity8U, CV_8UC1, 255/(maxVal - minVal));

    cv::normalize(imgDisparity16S, imgDisparity8U, 0, 255, CV_MINMAX, CV_8U);

    cv::imshow("Matching Debug1",imgDisparity8U);
    cv::imshow("Matching Debug2",Image_cam0_gs);

    cv::waitKey(1);

    // update settings
    //updateSettings(settings);

}

void StereoMatching::updateSettings(StereoMatchingSettings settings) {
	this->settings = settings;
}


int StereoMatching::loadCalibrationFile(StereoMatchingSettings settings) {

	// Open file with undistort matrices.
	FileStorage fs(settings->loadFileName_intrinsic, FileStorage::READ);
	// Check file.
	if (!fs.isOpened()) {
		return (false);
	}

	fs["M1"] >> M1;
	fs["D1"] >> D1;
	fs["M2"] >> M2;
	fs["D2"] >> D2;

	// Close file.
	fs.release();

	FileStorage fs1(settings->loadFileName_extrinsic, FileStorage::READ);
	// Check file.
	if (!fs1.isOpened()) {
		return (false);
	}
    fs1["R"] >> R;
    fs1["T"] >> T;

	fs1.release();

	return (true);
}
