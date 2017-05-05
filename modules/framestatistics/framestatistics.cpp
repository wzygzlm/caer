#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/frame.h>
#include <libcaercpp/events/frame.hpp>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
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
static void caerFrameStatisticsReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions FrameStatisticsFunctions = { .moduleInit = &caerFrameStatisticsInit,
	.moduleRun = &caerFrameStatisticsRun, .moduleConfig = NULL, .moduleExit = &caerFrameStatisticsExit, .moduleReset =
		&caerFrameStatisticsReset };

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

	// Configurable division factor.
	sshsNodeCreateInt(moduleData->moduleNode, "numBins", 512, 4, UINT16_MAX + 1, SSHS_FLAGS_NORMAL);
	state->numBins = sshsNodeGetInt(moduleData->moduleNode, "numBins");

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
		const cv::Mat frameOpenCV = frame.getOpenCVMat();

		const float range[] = {0, UINT16_MAX + 1};
		const float *histRange = { range };

		cv::Mat hist;
		cv::calcHist(&frameOpenCV, 1, nullptr, cv::Mat(), hist, 1, &state->numBins, &histRange, true, false);
	}
}

static void caerFrameStatisticsExit(caerModuleData moduleData) {

}

static void caerFrameStatisticsReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

}
