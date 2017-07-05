/*
 *
 *  Created on: Jan, 2017
 *      Author: federico.corradi@inilabs.com
 */

#include <time.h>
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include "ext/portable_time.h"
#include "ext/colorjet/colorjet.h"
#include <libcaer/devices/dvs128.h>
#include <libcaer/devices/davis.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h> //display

struct MRFilter_state {
	caerDeviceHandle eventSourceDeviceHandle;
	sshsNode eventSourceConfigNode;
	simple2DBufferFloat frequencyMap;
	simple2DBufferLong spikeCountMap;
	int8_t subSampleBy;
	int32_t colorscaleMax;
	int32_t colorscaleMin;
	float targetFreq;
	double measureMinTime;
	double measureStartedAt;
	bool startedMeas;
	bool doSetFreq;
	struct timespec tstart;
	struct timespec tend;
	int16_t sourceID;
};

typedef struct MRFilter_state *MRFilterState;

static bool caerMeanRateFilterInit(caerModuleData moduleData);
static void caerMeanRateFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerMeanRateFilterConfig(caerModuleData moduleData);
static void caerMeanRateFilterExit(caerModuleData moduleData);
static void caerMeanRateFilterReset(caerModuleData moduleData, uint16_t resetCallSourceID);
static bool allocateFrequencyMap(MRFilterState state, int16_t sourceID);
static bool allocateSpikeCountMap(MRFilterState state, int16_t sourceID);

static struct caer_module_functions caerMeanRateFilterFunctions = { .moduleInit = &caerMeanRateFilterInit, .moduleRun =
	&caerMeanRateFilterRun, .moduleConfig = &caerMeanRateFilterConfig, .moduleExit = &caerMeanRateFilterExit,
	.moduleReset = &caerMeanRateFilterReset };

static const struct caer_event_stream_in moduleInputs[] = {
    { .type = POLARITY_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "MeanRateDVS",
	.description = "Measure mean rate activity of dvs pixels",
	.type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct MRFilter_state),
	.functions = &caerMeanRateFilterFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = moduleOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs)
};

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}


static bool caerMeanRateFilterInit(caerModuleData moduleData) {

	MRFilterState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	state->sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateInt(moduleData->moduleNode, "colorscaleMax", 500, 0, 1000, SSHS_FLAGS_NORMAL, "Color Scale, i.e. Max Frequency (Hz)");
	sshsNodeCreateInt(moduleData->moduleNode, "colorscaleMin", 0, 0, 1000, SSHS_FLAGS_NORMAL, "Color Scale, i.e. Min Frequency (Hz)");
	sshsNodeCreateFloat(moduleData->moduleNode, "targetFreq", 100, 0, 250, SSHS_FLAGS_NORMAL, "Target frequency for neurons");
	sshsNodeCreateFloat(moduleData->moduleNode, "measureMinTime", 3.0, 0, 360, SSHS_FLAGS_NORMAL, "Measure time before updating the mean");
	sshsNodeCreateBool(moduleData->moduleNode, "doSetFreq", false, SSHS_FLAGS_NORMAL, "Start/Stop changing biases for reaching target frequency"); // TODO not implemented

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// internals
	state->startedMeas = false;
	state->measureStartedAt = 0.0f;
	state->measureMinTime = sshsNodeGetFloat(moduleData->moduleNode, "measureMinTime");

	allocateFrequencyMap(state, state->sourceID);
	allocateSpikeCountMap(state, state->sourceID);

	sshsNode sourceInfoNodeCA = caerMainloopGetSourceInfo(state->sourceID);
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodePutShort(sourceInfoNode, "dataSizeX", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeX"));
		sshsNodePutShort(sourceInfoNode, "dataSizeY", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeY"));
	}

	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(state->sourceID));

	// Nothing that can fail here.
	return (true);
}

static void caerMeanRateFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {

	caerPolarityEventPacketConst polarity =
		(caerPolarityEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	MRFilterState state = moduleData->moduleState;

	// if not measuring, let's start
	if (state->startedMeas == false) {
		portable_clock_gettime_monotonic(&state->tstart);
		state->measureStartedAt = (double) state->tstart.tv_sec + 1.0e-9 * state->tstart.tv_nsec;
		state->startedMeas = true;
	}

	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(caerEventPacketHeaderGetEventSource(&polarity->packetHeader));

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	// get current time
	portable_clock_gettime_monotonic(&state->tend);
	double now = ((double) state->tend.tv_sec + 1.0e-9 * state->tend.tv_nsec);
	// if we measured for enough time..
	if (state->measureMinTime <= (now - state->measureStartedAt)) {

		//caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "\nfreq measurement completed.\n");
		state->startedMeas = false;

		//update frequencyMap
		for (size_t x = 0; x < sizeX; x++) {
			for (size_t y = 0; y < sizeY; y++) {
				if (state->measureMinTime > 0) {
					state->frequencyMap->buffer2d[x][y] = (float) state->spikeCountMap->buffer2d[x][y]
						/ (float) state->measureMinTime;
				}
				//reset
				state->spikeCountMap->buffer2d[x][y] = 0;
			}
		}

		// set the biases if asked
		if (state->doSetFreq) {
			//TODO: NOT IMPLEMENTED
		}
	}

	// update filter parameters
	caerMeanRateFilterConfig(moduleData);

	// Iterate over events and update frequency
	CAER_POLARITY_ITERATOR_VALID_START(polarity)
	// Get values on which to operate.
		int64_t ts = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

		// Update value into maps 
		state->spikeCountMap->buffer2d[x][y] += 1;
	CAER_POLARITY_ITERATOR_VALID_END


	// Generate output frame.
	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
		return; // Error.
	}

	// Everything that is in the out packet container will be automatically freed after main loop.
	caerFrameEventPacket frameOut = caerFrameEventPacketAllocate(1, moduleData->moduleID,
		caerEventPacketHeaderGetEventTSOverflow(&polarity->packetHeader), I32T(sizeX),
		I32T(sizeY), 3);
	if (frameOut == NULL) {
		return; // Error.
	}
	else {
		// Add output packet to packet container.
		caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) frameOut);
	}

	// Make image.
	caerFrameEvent singleplot = caerFrameEventPacketGetEvent(frameOut, 0);

	size_t counter = 0;
	for (size_t x = 0; x < sizeX; x++) {
		for (size_t y = 0; y < sizeY; y++) {
			//uint16_t colorValue = U16T(state->surfaceMap->buffer2d[x][y] * UINT16_MAX);
			COLOUR col  = GetColour((double) state->frequencyMap->buffer2d[y][x], state->colorscaleMin, state->colorscaleMax);
			singleplot->pixels[counter] = (int) (col.r*65535); // red
			singleplot->pixels[counter + 1] = (int) (col.g*65535); // green
			singleplot->pixels[counter + 2] = (int) (col.b*65535); // blue
			counter += 3;
		}
	}

	// Add info to frame.
	caerFrameEventSetLengthXLengthYChannelNumber(singleplot, I32T(sizeX),
		I32T(sizeY), 3, frameOut);
	// Validate frame.
	caerFrameEventValidate(singleplot, frameOut);


}

static void caerMeanRateFilterConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	MRFilterState state = moduleData->moduleState;

	state->colorscaleMax = sshsNodeGetInt(moduleData->moduleNode, "colorscaleMax");
	state->colorscaleMin = sshsNodeGetInt(moduleData->moduleNode, "colorscaleMin");
	state->targetFreq = sshsNodeGetFloat(moduleData->moduleNode, "targetFreq");
	state->measureMinTime = sshsNodeGetFloat(moduleData->moduleNode, "measureMinTime");
	state->doSetFreq = sshsNodeGetBool(moduleData->moduleNode, "doSetFreq");

}

static void caerMeanRateFilterExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	MRFilterState state = moduleData->moduleState;

	// Ensure maps are freed.
	simple2DBufferFreeFloat(state->frequencyMap);
	simple2DBufferFreeLong(state->spikeCountMap);
}

static void caerMeanRateFilterReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	MRFilterState state = moduleData->moduleState;

	// Reset maps to all zeros (startup state).
	simple2DBufferResetLong(state->spikeCountMap);
	simple2DBufferResetFloat(state->frequencyMap);
}

static bool allocateSpikeCountMap(MRFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	state->spikeCountMap = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->spikeCountMap == NULL) {
		return (false);
	}

	for (size_t x = 0; x < sizeX; x++) {
		for (size_t y = 0; y < sizeY; y++) {
			state->spikeCountMap->buffer2d[x][y] = 0; // init to zero
		}
	}

	return (true);
}

static bool allocateFrequencyMap(MRFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	state->frequencyMap = simple2DBufferInitFloat((size_t) sizeX, (size_t) sizeY);
	if (state->frequencyMap == NULL) {
		return (false);
	}

	for (size_t x = 0; x < sizeX; x++) {
		for (size_t y = 0; y < sizeY; y++) {
			state->frequencyMap->buffer2d[x][y] = 0.0f; // init to zero
		}
	}

	return (true);
}

