/*
 * caerSpikeFeatures
 *
 *  Created on: May 1, 2017
 *      Author: federico @ Capo Caccia with Andre'
 */

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"

#define num_features_map 50
#define map_size 11

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
static void caerSpikeFeaturesRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerSpikeFeaturesConfig(caerModuleData moduleData);
static void caerSpikeFeaturesExit(caerModuleData moduleData);
static void caerSpikeFeaturesReset(caerModuleData moduleData, int16_t resetCallSourceID);
static bool allocateFeaturesMap(SFFilterState state, int16_t sourceID);
static bool allocateSurfaceMap(SFFilterState state, int16_t sourceID);
static bool allocateSurfaceMapLastTs(SFFilterState state, int16_t sourceID);

static struct caer_module_functions caerSpikeFeaturesFunctions = { .moduleInit = &caerSpikeFeaturesInit, .moduleRun =
	&caerSpikeFeaturesRun, .moduleConfig = &caerSpikeFeaturesConfig, .moduleExit = &caerSpikeFeaturesExit,
	.moduleReset = &caerSpikeFeaturesReset };


static const struct caer_event_stream_in moduleInputs[] = {
    { .type = POLARITY_EVENT, .number = 1, .readOnly = true },
};

static const struct caer_event_stream_out moduleOutputs[] = {
    { .type = FRAME_EVENT }
};

static const struct caer_module_info moduleInfo = {
    .version = 1,
    .name = "SpikeFeatures",
    .type = CAER_MODULE_PROCESSOR,
    .memSize = sizeof(struct SFFilter_state),
    .functions = &caerSpikeFeaturesFunctions,
    .inputStreams = moduleInputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
    .outputStreams = moduleOutputs,
    .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs)
};

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}

static bool caerSpikeFeaturesInit(caerModuleData moduleData) {
	sshsNodeCreateInt(moduleData->moduleNode, "decayTime", 3, 0, 2000, SSHS_FLAGS_NORMAL);
	sshsNodeCreateFloat(moduleData->moduleNode, "tau", 0.02, 0, 100, SSHS_FLAGS_NORMAL);

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

static void caerSpikeFeaturesRun(caerModuleData moduleData, caerEventPacketContainer in,
    caerEventPacketContainer *out){


	caerPolarityEventPacketConst polarity = (caerPolarityEventPacketConst)
	    caerEventPacketContainerFindEventPacketByTypeConst(in, POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	int32_t sourceID = caerEventPacketHeaderGetEventSource(&polarity->packetHeader);
	sshsNode sourceInfoNodeCA = caerMainloopGetSourceInfo(sourceID);
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodeCreateShort(sourceInfoNode, "dataSizeX", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeX"), 1, 1024,
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
		sshsNodeCreateShort(sourceInfoNode, "dataSizeY", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeY"), 1, 1024,
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
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
	CAER_POLARITY_CONST_ITERATOR_VALID_START (polarity)
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
	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
	    return; // Error.
	}

	// everything that is in the out packet container will be automatically be free after main loop
	caerFrameEventPacket frameOut = caerFrameEventPacketAllocate(1, moduleData->moduleID, 0, sizeX, sizeY, 3);
	if (frameOut == NULL) {
	    return; // Error.
	}
	else {
	    // Add output packet to packet container.
	    caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) frameOut);
	}


	// put info into frame
	caerFrameEvent singleplot = caerFrameEventPacketGetEvent(frameOut, 0);
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
	caerFrameEventSetLengthXLengthYChannelNumber(singleplot, sizeX, sizeY, 3, frameOut);
	//validate frame
	caerFrameEventValidate(singleplot, frameOut);


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

static void caerSpikeFeaturesReset(caerModuleData moduleData, int16_t resetCallSourceID) {
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

