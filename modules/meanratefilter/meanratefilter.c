/*
 *
 *  Created on: Dec, 2016
 *      Author: federico.corradi@inilabs.com
 */

#include <time.h>
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include "ext/portable_time.h"
#include "libcaer/devices/dynapse.h"
#include "ext/colorjet/colorjet.h"
#include "modules/ini/dynapse_common.h"
#include <libcaer/events/spike.h>
#include <libcaer/events/frame.h> //display

struct MRFilter_state {
	caerInputDynapseState eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	simple2DBufferFloat frequencyMap;
	simple2DBufferLong spikeCountMap;
	int8_t subSampleBy;
	int32_t colorscaleMax;
	int32_t colorscaleMin;
	float targetFreq;
	float measureMinTime;
	double measureStartedAt;
	bool startedMeas;
	bool doSetFreq;
	struct timespec tstart;			//struct is defined in gen_spike.c
	struct timespec tend;
	int16_t sourceID;
};

typedef struct MRFilter_state *MRFilterState;

static bool caerMeanRateFilterInit(caerModuleData moduleData);
static void caerMeanRateFilterRun(caerModuleData moduleData, caerEventPacketContainer in,
    caerEventPacketContainer *out);
static void caerMeanRateFilterConfig(caerModuleData moduleData);
static void caerMeanRateFilterExit(caerModuleData moduleData);
static void caerMeanRateFilterReset(caerModuleData moduleData, uint16_t resetCallSourceID);
static bool allocateFrequencyMap(MRFilterState state, int16_t sourceID);
static bool allocateSpikeCountMap(MRFilterState state, int16_t sourceID);

static struct caer_module_functions caerMeanRateFilterFunctions = { .moduleInit =
	&caerMeanRateFilterInit, .moduleRun = &caerMeanRateFilterRun, .moduleConfig =
	&caerMeanRateFilterConfig, .moduleExit = &caerMeanRateFilterExit, .moduleReset =
	&caerMeanRateFilterReset };

static const struct caer_event_stream_in moduleInputs[] = {
    { .type = SPIKE_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "MeanRate",
	.description = "Measure mean rate activity of neurons",
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
	sshsNodeCreateBool(moduleData->moduleNode, "doSetFreq", false, SSHS_FLAGS_NORMAL, "Start/Stop changing biases for reaching target frequency");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodeCreateShort(sourceInfoNode, "dataSizeX", DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX*16, SSHS_FLAGS_NORMAL, "number of neurons in X");
		sshsNodeCreateShort(sourceInfoNode, "dataSizeY", DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY*16, SSHS_FLAGS_NORMAL, "number of neurons in Y");
	}

	// internals
	state->startedMeas = false;
	state->measureStartedAt = 0.0;
	state->measureMinTime = sshsNodeGetFloat(moduleData->moduleNode, "measureMinTime");

	allocateFrequencyMap(state, state->sourceID);
	allocateSpikeCountMap(state, state->sourceID);

	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(state->sourceID));

	// Nothing that can fail here.
	return (true);
}

static void caerMeanRateFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerSpikeEventPacketConst spike =
		(caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, SPIKE_EVENT);

	// Only process packets with content.
	if (spike == NULL) {
		return;
	}

	MRFilterState state = moduleData->moduleState;

	// if not measuring, let's start
	if( state->startedMeas == false ){
		portable_clock_gettime_monotonic(&state->tstart);
		state->measureStartedAt = (double) state->tstart.tv_sec + 1.0e-9 * state->tstart.tv_nsec;
		state->startedMeas = true;
	}

	sshsNode sourceInfoNode = caerMainloopGetSourceInfo((uint16_t)caerEventPacketHeaderGetEventSource(&spike->packetHeader));

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	// get current time
	portable_clock_gettime_monotonic(&state->tend);
	double now = ((double) state->tend.tv_sec + 1.0e-9 * state->tend.tv_nsec);
	// if we measured for enough time..
	if( (double) state->measureMinTime <= (double) (now - state->measureStartedAt) ){

		//caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "\nfreq measurement completed.\n");
		state->startedMeas = false;

		//update frequencyMap
		for (int16_t x = 0; x < sizeX; x++) {
			for (int16_t y = 0; y < sizeY; y++) {
				if(state->measureMinTime > 0){
					state->frequencyMap->buffer2d[x][y] = (float)state->spikeCountMap->buffer2d[x][y]/(float)state->measureMinTime;
				}
				//reset
				state->spikeCountMap->buffer2d[x][y] = 0;
			}
		}

		// set the biases if asked
		if(state->doSetFreq){

			//collect data for all cores
			float sum[DYNAPSE_X4BOARD_COREX][DYNAPSE_X4BOARD_COREY];
			float mean[DYNAPSE_X4BOARD_COREX][DYNAPSE_X4BOARD_COREY];
			float var[DYNAPSE_X4BOARD_COREX][DYNAPSE_X4BOARD_COREY];
			// init
			for(size_t x=0; x<DYNAPSE_X4BOARD_COREX; x++){
				for(size_t y=0; y<DYNAPSE_X4BOARD_COREY; y++){
					sum[x][y] = 0.0f;
					mean[x][y] = 0.0f;
					var[x][y] = 0.0f;
				}
			}
			float max_freq = 0.0f;
			//loop over all cores
			for(size_t corex=0; corex<DYNAPSE_X4BOARD_COREX; corex++){
				for(size_t corey=0; corey<DYNAPSE_X4BOARD_COREY; corey++){
					max_freq = 0.0f;
					//get sum from core
					for(size_t x=0+(corex*DYNAPSE_CONFIG_NEUROW);x<DYNAPSE_CONFIG_NEUROW+(corex*DYNAPSE_CONFIG_NEUROW);x++){
						for(size_t y=0+(corey*DYNAPSE_CONFIG_NEUCOL);y<DYNAPSE_CONFIG_NEUCOL+(corey*DYNAPSE_CONFIG_NEUCOL);y++){
							sum[corex][corey] += state->frequencyMap->buffer2d[x][y]; //Hz
							if(max_freq < state->frequencyMap->buffer2d[x][y]){
								max_freq = state->frequencyMap->buffer2d[x][y];
							}
						}
					}
					//calculate mean
					mean[corex][corey] = sum[corex][corey]/(float)DYNAPSE_CONFIG_NUMNEURONS_CORE;
					//calculate variance
					for(size_t x=0+(corex*DYNAPSE_CONFIG_NEUROW);x<DYNAPSE_CONFIG_NEUROW+(corex*DYNAPSE_CONFIG_NEUROW);x++){
						for(size_t y=0+(corey*DYNAPSE_CONFIG_NEUCOL);y<DYNAPSE_CONFIG_NEUCOL+(corey*DYNAPSE_CONFIG_NEUCOL);y++){
							float f = (state->frequencyMap->buffer2d[x][y]) - mean[corex][corey];
							var[corex][corey] += f*f;
						}
					}
					caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
							"\nmean[%d][%d] = %f Hz var[%d][%d] = %f  max_freq %f\n",
							(int) corex, (int) corey, (double)mean[corex][corey], (int) corex, (int) corey, (double) var[corex][corey], (double) max_freq);
				}
			}

			// now decide how to change the bias setting
			for(uint32_t corex=0; corex<DYNAPSE_X4BOARD_COREX; corex++){
				for(uint32_t corey=0; corey<DYNAPSE_X4BOARD_COREY; corey++){

					uint32_t chipid = 0;
					uint32_t coreid = 0;

					// which chip/core should we address ?
					if(corex < 2 && corey < 2 ){
						chipid = DYNAPSE_CONFIG_DYNAPSE_U0;
						coreid = corex << 1 | corey;
					}else if(corex < 2 && corey >= 2 ){
						chipid = DYNAPSE_CONFIG_DYNAPSE_U2;
						coreid = corex << 1 | (corey-2);
					}else if(corex >= 2 && corey < 2){
						chipid = DYNAPSE_CONFIG_DYNAPSE_U1;
						coreid = (corex-2) << 1 | corey;
					}else if(corex >= 2 && corey >= 2){
						chipid = DYNAPSE_CONFIG_DYNAPSE_U3;
						coreid = (corex-2) << 1 | (corey-2) ;
					}

					caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
						"\nmean[%d][%d] = %f Hz var[%d][%d] = %f chipid = %d coreid %d\n",
						(int) corex, (int) corey, (double)mean[corex][corey], (int) corex, (int) corey, (double) var[corex][corey], chipid, coreid);

					// current dc settings
					sshsNode chipNode = sshsGetRelativeNode(state->eventSourceConfigNode,chipIDToName((int16_t)chipid, true));
				    sshsNode biasNode = sshsGetRelativeNode(chipNode, "bias/");

				    // select right bias name
				    char biasName[] = "C0_IF_DC_P"; // "CX_IF_DC_P" max bias name length is 10
					if(coreid == 0){
						memcpy(biasName, "C0_IF_DC_P", 10);
					}else if(coreid == 1){
						memcpy(biasName, "C1_IF_DC_P", 10);
					}else if(coreid == 2){
						memcpy(biasName, "C2_IF_DC_P", 10);
					}else if(coreid == 3){
						memcpy(biasName, "C3_IF_DC_P", 10);
					}
					// Add trailing slash to node name (required!).
					size_t biasNameLength = strlen(biasName);
					char biasNameFull[biasNameLength + 2];
					memcpy(biasNameFull, biasName, biasNameLength);
					biasNameFull[biasNameLength] = '/';
					biasNameFull[biasNameLength + 1] = '\0';

					// Get biasConfNode for this particular bias.
					sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

					// Read current coarse and fine settings.
					uint8_t coarseValue = (uint8_t) sshsNodeGetByte(biasConfigNode, "coarseValue");
					uint16_t fineValue =  (uint16_t) sshsNodeGetShort(biasConfigNode, "fineValue");

					caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
											"\n BIAS %s coarse %d fine %d\n",
											biasNameFull, coarseValue, fineValue );

					bool changed = false;
					int step = 15; // fine step value
					// compare current frequency with target
					if( (state->targetFreq - mean[corex][corey]) > 0 ){
						// we need to increase freq.. increase fine
						if(fineValue < (255-step)){
							fineValue = (unsigned short ) fineValue + (unsigned short) step;
							changed = true;
						}else{
							// if we did not reach the max value
							if(coarseValue != 0){
								fineValue = (unsigned short) step;
								coarseValue += -1; // coarse 0 is max 7 is min
								changed = true;
							}else{
								caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
										"\n Reached Limit for Bias\n");
							}
						}
					}else if( (state->targetFreq - mean[corex][corey]) < 0){
						// we need to reduce freq
						if( (fineValue - step) > 0){
							fineValue = (unsigned short) fineValue - (unsigned short) step;
							changed = true;
						}else{
							// if we did not reach the max value
							if(coarseValue != 7){
								fineValue = (unsigned short) step;
								coarseValue += +1; // coarse 0 is max 7 is min
								changed = true;
							}else{
								caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
										"\n Reached Limit for Bias\n");
							}
						}
					}
					if(changed){
						//generate bits to send
						generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode,
								biasName, coarseValue, fineValue, "HighBias", "Normal", "PBias", true, chipid);
					}

				}
			}
		}
	}

	// update filter parameters
	caerMeanRateFilterConfig(moduleData);

	// Iterate over events and update frequency
	CAER_SPIKE_ITERATOR_VALID_START(spike)
		// Get values on which to operate.
		//int64_t ts = caerSpikeEventGetTimestamp64(caerSpikeIteratorElement, spike);
		uint16_t x = caerSpikeEventGetX(caerSpikeIteratorElement);
		uint16_t y = caerSpikeEventGetY(caerSpikeIteratorElement);

		// Update value into maps 
		state->spikeCountMap->buffer2d[x][y] += 1;

	CAER_SPIKE_ITERATOR_VALID_END

	// Generate output frame.
	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
		return; // Error.
	}

	// Everything that is in the out packet container will be automatically freed after main loop.
	caerFrameEventPacket frameOut = caerFrameEventPacketAllocate(1, moduleData->moduleID,
		caerEventPacketHeaderGetEventTSOverflow(&spike->packetHeader), I32T(sizeX),
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

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeClearSubTree(sourceInfoNode,true);

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

	for(int16_t x=0; x<sizeX; x++){
		for(int16_t y=0; y<sizeY; y++){
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

	for(int16_t x=0; x<sizeX; x++){
		for(int16_t y=0; y<sizeY; y++){
			state->frequencyMap->buffer2d[x][y] = 0.0f; // init to zero
		}
	}

	return (true);
}

