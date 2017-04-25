/*
 * depressingsynapsefilter.c
 *
 *  Created on: Apr. 2017
 *      Author: Tianyu
 */

#include "depressingsynapsefilter.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include "math.h"

struct DSFilter_state {
	float weight;
	float tauUs;
	simple2DBufferFloat neuronStateMap;
	simple2DBufferLong neuronLasttMap;
	simple2DBufferInt neuronIniMap;
};

typedef struct DSFilter_state *DSFilterState;

static const int maxState = 1;

static bool caerDepressingSynapsefilterInit(caerModuleData moduleData);
static void caerDepressingSynapsefilterRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerDepressingSynapsefilterConfig(caerModuleData moduleData);
static void caerDepressingSynapsefilterExit(caerModuleData moduleData);
static void caerDepressingSynapsefilterReset(caerModuleData moduleData, uint16_t resetCallSourceID);
static bool allocateNeuronStateMap(DSFilterState state, int16_t sourceID);
static bool allocateNeuronLasttMap(DSFilterState state, int16_t sourceID);
static bool allocateNeuronIniMap(DSFilterState state, int16_t sourceID);


static struct caer_module_functions caerDepressingSynapsefilterFunctions = { .moduleInit = &caerDepressingSynapsefilterInit, .moduleRun = &caerDepressingSynapsefilterRun, .moduleConfig = &caerDepressingSynapsefilterConfig, .moduleExit = &caerDepressingSynapsefilterExit, .moduleReset = &caerDepressingSynapsefilterReset };

void caerDepressingSynapseFilter(uint16_t moduleID, caerPolarityEventPacket polarity) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "DepressingFilter", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}
	caerModuleSM(&caerDepressingSynapsefilterFunctions, moduleData, sizeof(struct DSFilter_state), 2, polarity);
}

static bool caerDepressingSynapsefilterInit(caerModuleData moduleData) {
	sshsNodePutFloatIfAbsent(moduleData->moduleNode, "weight", 0.001f);
	sshsNodePutFloatIfAbsent(moduleData->moduleNode, "tauUs", 1000000.0f);

	DSFilterState state = moduleData->moduleState;

	state->weight= sshsNodeGetFloat(moduleData->moduleNode, "weight");
	state->tauUs = sshsNodeGetFloat(moduleData->moduleNode, "tauUs");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}
static void caerDepressingSynapsefilterRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	caerPolarityEventPacket polarity = va_arg(args, caerPolarityEventPacket);

	//Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	DSFilterState state = moduleData->moduleState;

	// If the map is not allocated yet, do it.
	if (state->neuronStateMap == NULL) {
		if (!allocateNeuronStateMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for timestampMap.");
			return;
		}
	}

	if (state->neuronLasttMap == NULL) {
		if (!allocateNeuronLasttMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for timestampMap.");
			return;
		}
	}

	if (state->neuronIniMap == NULL) {
		if (!allocateNeuronIniMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for timestampMap.");
			return;
		}
	}


	int16_t sourceID = caerEventPacketHeaderGetEventSource(&polarity->packetHeader);
	sshsNode sourceInfoNodeCA = caerMainloopGetSourceInfo((uint16_t)sourceID);
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) {
		sshsNodePutShort(sourceInfoNode, "dataSizeX", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeX"));
		sshsNodePutShort(sourceInfoNode, "dataSizeY", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeY"));
	}
	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	//Iterate over events
	CAER_POLARITY_ITERATOR_VALID_START(polarity)

	// Get values on which to operate.
	int64_t ts = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
	uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
	uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

	if ((caerPolarityIteratorElement == NULL)){
		continue;
	}
	if ((x >= sizeX) || (y >= sizeY)) {
		continue;
	}

	// update states
	if (state->neuronIniMap->buffer2d[x][y] == 0){
		state->neuronLasttMap = ts;
		state->neuronIniMap->buffer2d[x][y] = 1;
	}

	if (ts < state->neuronLasttMap[x][y]){
		state->neuronIniMap->buffer2d[x][y] = 0;
		state->neuronStateMap->buffer2d[x][y] = 0;
	}

	int64_t dt = ts - state->neuronLasttMap->buffer2d[x][y];
	float delta = dt / state->tauUs;
	float expValue = delta > 20 ? 0 : (float) exp(-delta);
	float newstate = (state->neuronStateMap->buffer2d[x][y] * expValue) + state->weight;
	if (newstate > maxState){
		newstate = maxState;
	}
	bool spike = (rand() / RAND_MAX) > state->neuronStateMap->buffer2d[x][y];
	if (!spike){
		caerPolarityEventInvalidate(caerPolarityIteratorElement, polarity);
	}
	state->neuronStateMap->buffer2d[x][y] = newstate;
	state->neuronLasttMap->buffer2d[x][y] = ts;

	CAER_POLARITY_ITERATOR_VALID_END

}


static void caerDepressingSynapsefilterConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	RFilterState state = moduleData->moduleState;
	state->swapXY= sshsNodeGetBool(moduleData->moduleNode, "swapXY");
	state->rotate90deg = sshsNodeGetBool(moduleData->moduleNode, "rotate90deg");
	state->invertX = sshsNodeGetBool(moduleData->moduleNode, "invertX");
	state->invertY = sshsNodeGetBool(moduleData->moduleNode, "invertY");
	state->angleDeg = sshsNodeGetFloat(moduleData->moduleNode, "angleDeg");
}

static void caerDepressingSynapsefilterExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

}

static void caerDepressingSynapsefilterReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);
}

static bool allocateNeuronStateMap(DSFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate neuronState map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	state->neuronStateMap = simple2DBufferInitFloat((size_t) sizeX, (size_t) sizeY);
	if (state->neuronStateMap == NULL) {
		return (false);
	}

	return (true);
}

static bool allocateNeuronLasttMap(DSFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate neuronState map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	state->neuronLasttMap = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->neuronLasttMap == NULL) {
		return (false);
	}

	return (true);
}

static bool allocateNeuronIniMap(DSFilterState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate neuronState map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");

	state->neuronIniMap = simple2DBufferInitInt((size_t) sizeX, (size_t) sizeY);
	if (state->neuronIniMap == NULL) {
		return (false);
	}

	return (true);
}
