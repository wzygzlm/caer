/*
 * caerSpikeFeatures
 *
 *  Created on: May 1, 2017
 *      Author: federico @ Capo Caccia with Andre'
 */

#include "spikefeatures.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"

struct SFFilter_state {
	simple2DBufferLong featuresMap[num_features_map];
	int32_t deltaT;
	simple2DBufferFloat surfaceMap;			// surface time
	simple2DBufferLong surfaceMapLastTs;	// surface time
	int32_t decayTime;						// time constant for the delay
	float tau;
	int64_t lastTimeStamp;
};

typedef struct SFFilter_state *SFFilterState;

static bool caerSpikeFeaturesInit(caerModuleData moduleData);
static void caerSpikeFeaturesRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerSpikeFeaturesConfig(caerModuleData moduleData);
static void caerSpikeFeaturesExit(caerModuleData moduleData);
static void caerSpikeFeaturesReset(caerModuleData moduleData, uint16_t resetCallSourceID);
static bool allocateFeaturesMap(SFFilterState state, int16_t sourceID);
static bool allocateSurfaceMap(SFFilterState state, int16_t sourceID);
static bool allocateSurfaceMapLastTs(SFFilterState state, int16_t sourceID);

static struct caer_module_functions caerSpikeFeaturesFunctions = { .moduleInit = &caerSpikeFeaturesInit, .moduleRun =
	&caerSpikeFeaturesRun, .moduleConfig = &caerSpikeFeaturesConfig, .moduleExit = &caerSpikeFeaturesExit,
	.moduleReset = &caerSpikeFeaturesReset };

void caerSpikeFeatures(uint16_t moduleID, caerPolarityEventPacket polarity, caerFrameEventPacket *imagegeneratorframe) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "SpikeFeatures", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}

	caerModuleSM(&caerSpikeFeaturesFunctions, moduleData, sizeof(struct SFFilter_state), 2, polarity, imagegeneratorframe);
}

static bool caerSpikeFeaturesInit(caerModuleData moduleData) {
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "decayTime", 3);
	sshsNodePutFloatIfAbsent(moduleData->moduleNode, "tau", 0.02);

	SFFilterState state = moduleData->moduleState;

	state->decayTime = sshsNodeGetInt(moduleData->moduleNode, "decayTime");
	state->tau = sshsNodeGetFloat(moduleData->moduleNode, "tau");
	state->lastTimeStamp = 0;
	state->surfaceMap = NULL;
	state->surfaceMapLastTs = NULL;

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerSpikeFeaturesRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).
	caerPolarityEventPacket polarity = va_arg(args, caerPolarityEventPacket);
	caerFrameEventPacket *imagegeneratorframe = va_arg(args, caerFrameEventPacket*);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	int32_t sourceID = caerEventPacketHeaderGetEventSource(&polarity->packetHeader);
	sshsNode sourceInfoNodeCA = caerMainloopGetSourceInfo(sourceID);
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodePutShort(sourceInfoNode, "dataSizeX", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeX"));
		sshsNodePutShort(sourceInfoNode, "dataSizeY", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeY"));
	}
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		return;
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeY");

	SFFilterState state = moduleData->moduleState;

	// If the map is not allocated yet, do it.
	if (state->surfaceMap == NULL) {
		if (!allocateSurfaceMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for surfaceMap.");
			return;
		}
	}
	if (state->surfaceMapLastTs == NULL) {
		if (!allocateSurfaceMapLastTs(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for surfaceMapLastTs.");
			return;
		}
	}

	// Iterate over events and filter out ones that are not supported by other
	// events within a certain region in the specified timeframe.
	int64_t ts = 0;
	CAER_POLARITY_ITERATOR_VALID_START (polarity)
		// Get values on which to operate.
		ts = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);
		state->surfaceMap->buffer2d[x][y] = 1;
	CAER_POLARITY_ITERATOR_VALID_END

	//decay the map
	for (size_t x = 0; x < sizeX; x++) {
		for (size_t y = 0; y < sizeY; y++) {
			state->surfaceMapLastTs->buffer2d[x][y] = ts;
			if(state->surfaceMap->buffer2d[x][y] == 0){
				continue;
			}else{
				int64_t dt = (state->surfaceMapLastTs->buffer2d[x][y]  - ts);
				float decay = state->tau * dt;
				//printf("decay %f\n", decay);
				state->surfaceMap->buffer2d[x][y] -= state->tau; // decay
				if(state->surfaceMap->buffer2d[x][y]  < 0){
					state->surfaceMap->buffer2d[x][y]  = 0;
				}
			}
		}
	}

	state->lastTimeStamp = ts;

	//make frame
	// put info into frame
	*imagegeneratorframe = caerFrameEventPacketAllocate(1, I16T(moduleData->moduleID), 0, sizeX, sizeY, 3);
	caerMainloopFreeAfterLoop(&free, *imagegeneratorframe);
	if (*imagegeneratorframe != NULL) {
		caerFrameEvent singleplot = caerFrameEventPacketGetEvent(*imagegeneratorframe, 0);
		uint32_t counter = 0;
		for (size_t y = 0; y < sizeY; y++) {
			for (size_t x = 0; x < sizeX; x++) {
				singleplot->pixels[counter] = (uint16_t) ((int) (state->surfaceMap->buffer2d[x][y] * 65530)); // red
				singleplot->pixels[counter + 1] = (uint16_t) ((int) (state->surfaceMap->buffer2d[x][y] * 65530)); // green
				singleplot->pixels[counter + 2] = (uint16_t) ((int) (state->surfaceMap->buffer2d[x][y] * 65530)); // blue
				counter += 3;
			}
		}

		//add info to the frame
		caerFrameEventSetLengthXLengthYChannelNumber(singleplot, sizeX, sizeY, 3, *imagegeneratorframe);
		//validate frame
		caerFrameEventValidate(singleplot, *imagegeneratorframe);
	}

}

static void caerSpikeFeaturesConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	SFFilterState state = moduleData->moduleState;

	state->decayTime = sshsNodeGetInt(moduleData->moduleNode, "decayTime");
	state->tau = sshsNodeGetFloat(moduleData->moduleNode, "tau");
}

static void caerSpikeFeaturesExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	SFFilterState state = moduleData->moduleState;

	// Ensure map is freed.
	for (size_t i = 0; i < num_features_map; i++) {
		simple2DBufferFreeLong(state->featuresMap[i]);
	}
}

static void caerSpikeFeaturesReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	SFFilterState state = moduleData->moduleState;

	// Reset timestamp map to all zeros (startup state).
	for (size_t i = 0; i < num_features_map; i++) {
		simple2DBufferResetLong(state->featuresMap[i]);
	}
}

static bool allocateSurfaceMapLastTs(SFFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate timestamp map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	state->surfaceMapLastTs = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->surfaceMapLastTs == NULL) {
		return (false);
	}

	// TODO: size the map differently if subSampleBy is set!
	return (true);
}

static bool allocateSurfaceMap(SFFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate timestamp map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	state->surfaceMap = simple2DBufferInitFloat((size_t) sizeX, (size_t) sizeY);
	if (state->surfaceMap == NULL) {
		return (false);
	}

	return (true);
}

static bool allocateFeaturesMap(SFFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate timestamp map.");
		return (false);
	}

	int16_t sizeX = map_size;
	int16_t sizeY = map_size;

	for (size_t i = 0; i < num_features_map; i++) {
		state->featuresMap[i] = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
		if (state->featuresMap[i] == NULL) {
			return (false);
		}
	}
	return (true);
}

