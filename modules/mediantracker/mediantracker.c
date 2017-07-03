#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include <math.h>

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>
#include <libcaer/events/point4d.h>

struct MTFilter_state {
	float xmedian;
	float ymedian;
	float xstd;
	float ystd;
	float xmean;
	float ymean;
	int64_t lastts;
	int64_t dt;
	int64_t prevlastts;
	float radius;
	float numStdDevsForBoundingBox;
	int tauUs;
	int16_t sizeX;
	int16_t sizeY;
};

static const int TICK_PER_MS = 1000;

typedef struct MTFilter_state *MTFilterState;

static bool caerMediantrackerInit(caerModuleData moduleData);
static void caerMediantrackerRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerMediantrackerConfig(caerModuleData moduleData);
static void caerMediantrackerExit(caerModuleData moduleData);

static const struct caer_module_functions caerMediantrackerFunctions = { .moduleInit = &caerMediantrackerInit,
	.moduleRun = &caerMediantrackerRun, .moduleConfig = &caerMediantrackerConfig, .moduleExit = &caerMediantrackerExit };

static const struct caer_event_stream_in caerMediantrackerInputs[] = { { .type = POLARITY_EVENT, .number = 1,
	.readOnly = true } };

static const struct caer_event_stream_out caerMediantrackerOutputs[] = { { .type = FRAME_EVENT }, { .type =
	POINT4D_EVENT } };

static const struct caer_module_info caerMediantrackerInfo = { .version = 1, .name = "MedianTracker", .description =
	"Tracks an object by finding the median of event activity.", .type = CAER_MODULE_PROCESSOR, .memSize =
	sizeof(struct MTFilter_state), .functions = &caerMediantrackerFunctions, .inputStreams = caerMediantrackerInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(caerMediantrackerInputs), .outputStreams = caerMediantrackerOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(caerMediantrackerOutputs) };

caerModuleInfo caerModuleGetInfo(void) {
	return (&caerMediantrackerInfo);
}

static bool caerMediantrackerInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateInt(moduleData->moduleNode, "tauUs", 25, 0, 1000, SSHS_FLAGS_NORMAL, "TODO.");
	sshsNodeCreateFloat(moduleData->moduleNode, "numStdDevsForBoundingBox", 1.0f, 0.0f, 10.0f, SSHS_FLAGS_NORMAL,
		"TODO.");

	MTFilterState state = moduleData->moduleState;

	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	state->sizeX = sshsNodeGetShort(sourceInfo, "polaritySizeX");
	state->sizeY = sshsNodeGetShort(sourceInfo, "polaritySizeY");

	state->radius = 10.0f;

	caerMediantrackerConfig(moduleData);

	// Create own sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", state->sizeX, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output frame width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", state->sizeY, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output frame height.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", state->sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", state->sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Data height.");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerMediantrackerRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	// Interpret variable arguments (same as above in main function).
	caerPolarityEventPacketConst polarity =
		(caerPolarityEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	MTFilterState state = moduleData->moduleState;

	// get the size of the packet
	int n = caerEventPacketHeaderGetEventNumber(&polarity->packetHeader);

	// get last time stamp of the packet
	// update dt and prevlastts
	int64_t maxLastTime = 0;
	CAER_POLARITY_CONST_ITERATOR_VALID_START(polarity)
		if (maxLastTime < caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity)) {
			maxLastTime = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
		}CAER_POLARITY_ITERATOR_VALID_END

	state->lastts = maxLastTime;
	state->dt = state->lastts - state->prevlastts;
	state->prevlastts = state->lastts;
	if (state->dt < 0) {
		state->dt = 0;
	}

	// save position of all events in the packet
	int xs[n];
	int ys[n];
	int index = 0;
	CAER_POLARITY_CONST_ITERATOR_VALID_START(polarity)
		xs[index] = caerPolarityEventGetX(caerPolarityIteratorElement);
		ys[index] = caerPolarityEventGetY(caerPolarityIteratorElement);
		index++;
	CAER_POLARITY_ITERATOR_VALID_END

	// get median
	int x, y;
	if (index % 2 != 0) { // odd number of events take middle one
		x = xs[index / 2];
		y = ys[index / 2];
	}
	else { // even number of events take avg of middle two
		x = (xs[index / 2 - 1] + xs[index / 2]) / 2;
		y = (ys[index / 2 - 1] + ys[index / 2]) / 2;
	}

	float fac = (float) state->dt / (float) state->tauUs / (float) TICK_PER_MS;
	if (fac > 1) {
		fac = 1;
	}
	state->xmedian = state->xmedian + (x - state->xmedian) * fac;
	state->ymedian = state->ymedian + (y - state->ymedian) * fac;

	// get mean
	int xsum = 0;
	int ysum = 0;
	for (int i = 0; i < index; i++) {
		xsum += xs[i];
		ysum += ys[i];
	}
	state->xmean = state->xmean + (xsum / n - state->xmean) * fac;
	state->ymean = state->ymean + (ysum / n - state->ymean) * fac;

	// get std
	float xvar = 0.0f;
	float yvar = 0.0f;
	float tmp;
	for (int i = 0; i < index; i++) {
		tmp = xs[i] - state->xmean;
		tmp *= tmp;
		xvar += tmp;

		tmp = ys[i] - state->ymean;
		tmp *= tmp;
		yvar += tmp;
	}
	if (index != 0) {
		xvar /= index;
		yvar /= index;
	}
	state->xstd = state->xstd + ((float) sqrt((double) xvar) - state->xstd) * fac;
	state->ystd = state->ystd + ((float) sqrt((double) yvar) - state->ystd) * fac;

	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(2);
	if (*out == NULL) {
		return; // Error.
	}

	caerPoint4DEventPacket medianData = caerPoint4DEventPacketAllocate(128, moduleData->moduleID,
		I32T(state->lastts >> 31));
	if (medianData == NULL) {
		return; // Error.
	}
	else {
		// Add output packet to packet container.
		caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) medianData);
	}

	// set timestamp for 4d event
	caerPoint4DEvent evt = caerPoint4DEventPacketGetEvent(medianData,
		caerEventPacketHeaderGetEventNumber(&medianData->packetHeader));

	caerPoint4DEventSetTimestamp(evt, state->lastts & INT32_MAX);

	caerPoint4DEventSetX(evt, state->xmean);
	caerPoint4DEventSetY(evt, state->ymean);
	caerPoint4DEventSetZ(evt, state->xstd);
	caerPoint4DEventSetW(evt, state->ystd);

	// validate event
	caerPoint4DEventValidate(evt, medianData);

	caerFrameEventPacket frame = caerFrameEventPacketAllocate(1, moduleData->moduleID, I32T(state->lastts >> 31),
		state->sizeX, state->sizeY, 3);
	if (frame == NULL) {
		return; // Error.
	}
	else {
		// Add output packet to packet container.
		caerEventPacketContainerSetEventPacket(*out, 1, (caerEventPacketHeader) frame);
	}

	caerFrameEvent singleplot = caerFrameEventPacketGetEvent(frame, 0);
	uint32_t counter = 0;
	for (size_t yy = 0; yy < state->sizeY; yy++) {
		for (size_t xx = 0; xx < state->sizeX; xx++) {
			if ((xx == (int) state->xmedian && yy == (int) state->ymedian)
				|| (xx == (int) (state->xmedian + state->xstd * state->numStdDevsForBoundingBox)
					&& yy <= (state->ymedian + state->ystd * state->numStdDevsForBoundingBox)
					&& yy >= (state->ymedian - state->ystd * state->numStdDevsForBoundingBox))
				|| (xx == (int) (state->xmedian - state->xstd * state->numStdDevsForBoundingBox)
					&& yy <= (state->ymedian + state->ystd * state->numStdDevsForBoundingBox)
					&& yy >= (state->ymedian - state->ystd * state->numStdDevsForBoundingBox))
				|| (yy == (int) (state->ymedian + state->ystd * state->numStdDevsForBoundingBox)
					&& xx <= (state->xmedian + state->xstd * state->numStdDevsForBoundingBox)
					&& xx >= (state->xmedian - state->xstd * state->numStdDevsForBoundingBox))
				|| (yy == (int) (state->ymedian - state->ystd * state->numStdDevsForBoundingBox)
					&& xx <= (state->xmedian + state->xstd * state->numStdDevsForBoundingBox)
					&& xx >= (state->xmedian - state->xstd * state->numStdDevsForBoundingBox))) {
				singleplot->pixels[counter] = 0;		// red
				singleplot->pixels[counter + 1] = 0;		// green
				singleplot->pixels[counter + 2] = UINT16_MAX;		// blue
			}
			else {
				singleplot->pixels[counter] = 0;			// red
				singleplot->pixels[counter + 1] = 0;		// green
				singleplot->pixels[counter + 2] = 0;	// blue
			}
			counter += 3;
		}
	}

	//add info to the frame
	caerFrameEventSetLengthXLengthYChannelNumber(singleplot, state->sizeX, state->sizeY, 3, frame);
	//validate frame
	caerFrameEventValidate(singleplot, frame);

	CAER_POLARITY_CONST_ITERATOR_VALID_START(polarity)
		int xxx = caerPolarityEventGetX(caerPolarityIteratorElement);
		int yyy = caerPolarityEventGetY(caerPolarityIteratorElement);
		int pol = caerPolarityEventGetPolarity(caerPolarityIteratorElement);
		int address = 3 * (yyy * state->sizeX + xxx);
		if (pol == 0) {
			singleplot->pixels[address] = UINT16_MAX; // red
			singleplot->pixels[address + 1] = 0; // green
			singleplot->pixels[address + 2] = 0; // blue
		}
		else {
			singleplot->pixels[address] = 0; // red
			singleplot->pixels[address + 1] = UINT16_MAX; // green
			singleplot->pixels[address + 2] = 0; // blue
		}CAER_POLARITY_ITERATOR_VALID_END
}

static void caerMediantrackerConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	MTFilterState state = moduleData->moduleState;

	state->tauUs = sshsNodeGetInt(moduleData->moduleNode, "tauUs");
	state->numStdDevsForBoundingBox = sshsNodeGetFloat(moduleData->moduleNode, "numStdDevsForBoundingBox");
}

static void caerMediantrackerExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);
}
