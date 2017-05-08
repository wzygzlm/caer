#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/frame.h>
#include <libcaercpp/events/frame.hpp>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

struct caer_frame_statistics_state {
	int numBins;
};

typedef struct caer_frame_statistics_state *caerFrameStatisticsState;

static bool caerFrameStatisticsInit(caerModuleData moduleData);
static void caerFrameStatisticsRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerFrameStatisticsExit(caerModuleData moduleData);
static void caerFrameStatisticsConfig(caerModuleData moduleData);

static const struct caer_module_functions FrameStatisticsFunctions = { .moduleInit = &caerFrameStatisticsInit,
	.moduleRun = &caerFrameStatisticsRun, .moduleConfig = &caerFrameStatisticsConfig, .moduleExit =
		&caerFrameStatisticsExit, .moduleReset = NULL };

static const struct caer_event_stream_in FrameStatisticsInputs[] = { { .type = FRAME_EVENT, .number = 1, .readOnly =
	true } };

static const struct caer_module_info FrameStatisticsInfo = { .version = 1, .name = "FrameStatistics", .type =
	CAER_MODULE_OUTPUT, .memSize = sizeof(struct caer_frame_statistics_state), .functions = &FrameStatisticsFunctions,
	.inputStreams = FrameStatisticsInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(FrameStatisticsInputs),
	.outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&FrameStatisticsInfo);
}

static bool caerFrameStatisticsInit(caerModuleData moduleData) {
	caerFrameStatisticsState state = (caerFrameStatisticsState) moduleData->moduleState;

	// Configurable number of bins.
	sshsNodeCreateInt(moduleData->moduleNode, "numBins", 256, 4, UINT16_MAX + 1, SSHS_FLAGS_NORMAL);
	state->numBins = sshsNodeGetInt(moduleData->moduleNode, "numBins");

	cv::namedWindow(moduleData->moduleSubSystemString, CV_WINDOW_AUTOSIZE);

	return (true);
}

static void caerFrameStatisticsRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	if (in == nullptr) {
		return;
	}

	const libcaer::events::FrameEventPacket frames(caerEventPacketContainerGetEventPacket(in, 0), false);

	caerFrameStatisticsState state = static_cast<caerFrameStatisticsState>(moduleData->moduleState);

	for (const auto &frame : frames) {
		const cv::Mat frameOpenCV = frame.getOpenCVMat(false);

		// Calculate histogram, full uint16 range.
		const float range[] = { 0, UINT16_MAX + 1 };
		const float *histRange = { range };

		cv::Mat hist;
		cv::calcHist(&frameOpenCV, 1, nullptr, cv::Mat(), hist, 1, &state->numBins, &histRange, true, false);

		// Generate histogram image, 640x480 pixels.
		int hist_w = 640;
		int hist_h = 480;
		int bin_w = cvRound((double) hist_w / state->numBins);

		cv::Mat histImage(hist_h, hist_w, CV_8UC1, cv::Scalar(0));

		// Normalize the result to [0, histImage.rows].
		cv::normalize(hist, hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat());

		// Draw the histogram.
		for (int i = 1; i < state->numBins; i++) {
			cv::line(histImage, cv::Point(bin_w * (i - 1), hist_h - cvRound(hist.at<float>(i - 1))),
				cv::Point(bin_w * (i), hist_h - cvRound(hist.at<float>(i))), cv::Scalar(255, 255, 255), 2, 8, 0);
		}

		// Simple display, just use OpenCV GUI.
		cv::imshow(moduleData->moduleSubSystemString, histImage);
		cv::waitKey(1);
	}
}

static void caerFrameStatisticsExit(caerModuleData moduleData) {
	cv::destroyWindow(moduleData->moduleSubSystemString);
}

static void caerFrameStatisticsConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	caerFrameStatisticsState state = (caerFrameStatisticsState) moduleData->moduleState;
	state->numBins = sshsNodeGetInt(moduleData->moduleNode, "numBins");
}
