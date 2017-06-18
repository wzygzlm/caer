/*
 * caerSpikeFeatures
 *
 *  Created on: May 1, 2017
 *      Author: federico @ Capo Caccia with Andre'
 */

#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

struct SFFilter_state {
	simple2DBufferFloat surfaceMap;			// surface map
	simple2DBufferLong surfaceMapLastTs;	// surface time
	int32_t decayTime;						// time constant for the decay
	float tau;
	int64_t lastTimeStamp;
};

typedef struct SFFilter_state *SFFilterState;

static bool caerSpikeFeaturesInit(caerModuleData moduleData);
static void caerSpikeFeaturesRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerSpikeFeaturesConfig(caerModuleData moduleData);
static void caerSpikeFeaturesExit(caerModuleData moduleData);
static void caerSpikeFeaturesReset(caerModuleData moduleData, int16_t resetCallSourceID);

static struct caer_module_functions caerSpikeFeaturesFunctions = { .moduleInit = &caerSpikeFeaturesInit, .moduleRun =
	&caerSpikeFeaturesRun, .moduleConfig = &caerSpikeFeaturesConfig, .moduleExit = &caerSpikeFeaturesExit,
	.moduleReset = &caerSpikeFeaturesReset };

static const struct caer_event_stream_in moduleInputs[] = { { .type = POLARITY_EVENT, .number = 1, .readOnly = true }, };

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "SpikeFeatures",
	.type = CAER_MODULE_PROCESSOR, .memSize = sizeof(struct SFFilter_state), .functions = &caerSpikeFeaturesFunctions,
	.inputStreams = moduleInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs), .outputStreams =
		moduleOutputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs) };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

static bool caerSpikeFeaturesInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateInt(moduleData->moduleNode, "decayTime", 3, 0, 2000, SSHS_FLAGS_NORMAL, "TODO.");
	sshsNodeCreateFloat(moduleData->moduleNode, "tau", 0.02f, 0.0f, 100.0f, SSHS_FLAGS_NORMAL, "TODO.");

	SFFilterState state = moduleData->moduleState;

	// Allocate map using info from sourceInfo.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfo, "polaritySizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfo, "polaritySizeY");

	state->surfaceMap = simple2DBufferInitFloat((size_t) sizeX, (size_t) sizeY);
	if (state->surfaceMap == NULL) {
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for surfaceMap.");
		return (false);
	}

	state->surfaceMapLastTs = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->surfaceMapLastTs == NULL) {
		simple2DBufferFreeFloat(state->surfaceMap);
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for surfaceMapLastTs.");
		return (false);
	}

	caerSpikeFeaturesConfig(moduleData);

	// Populate own sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);
	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY,
		"Output frame width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY,
		"Output frame height.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY,
		"Output data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY,
		"Output data height.");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerSpikeFeaturesRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerPolarityEventPacketConst polarity =
		(caerPolarityEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	SFFilterState state = moduleData->moduleState;

	// Iterate over events and filter out ones that are not supported by other
	// events within a certain region in the specified time-frame.
	int64_t ts = 0;

	CAER_POLARITY_CONST_ITERATOR_VALID_START(polarity)
	// Get values on which to operate.
		ts = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);

		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

		state->surfaceMap->buffer2d[x][y] = 1;
	CAER_POLARITY_ITERATOR_VALID_END

	// Decay the map.
	for (size_t x = 0; x < state->surfaceMap->sizeX; x++) {
		for (size_t y = 0; y < state->surfaceMap->sizeY; y++) {
			state->surfaceMapLastTs->buffer2d[x][y] = ts;

			if (state->surfaceMap->buffer2d[x][y] == 0) {
				continue;
			}
			else {
				// TODO: dt is always zero here? And decay never used?
				int64_t dt = (state->surfaceMapLastTs->buffer2d[x][y] - ts);
				float decay = state->tau * dt;

				state->surfaceMap->buffer2d[x][y] -= state->tau; // Do decay.
				if (state->surfaceMap->buffer2d[x][y] < 0) {
					state->surfaceMap->buffer2d[x][y] = 0;
				}
			}
		}
	}

	// TODO: last timestamp is unused.
	state->lastTimeStamp = ts;

	// Generate output frame.
	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
		return; // Error.
	}

	// Everything that is in the out packet container will be automatically freed after main loop.
	caerFrameEventPacket frameOut = caerFrameEventPacketAllocate(1, moduleData->moduleID,
		caerEventPacketHeaderGetEventTSOverflow(&polarity->packetHeader), I32T(state->surfaceMap->sizeX),
		I32T(state->surfaceMap->sizeY), 3);
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
	for (size_t y = 0; y < state->surfaceMap->sizeY; y++) {
		for (size_t x = 0; x < state->surfaceMap->sizeX; x++) {
			uint16_t colorValue = U16T(state->surfaceMap->buffer2d[x][y] * UINT16_MAX);
			singleplot->pixels[counter] = colorValue; // red
			singleplot->pixels[counter + 1] = colorValue; // green
			singleplot->pixels[counter + 2] = colorValue; // blue
			counter += 3;
		}
	}

	// Add info to frame.
	caerFrameEventSetLengthXLengthYChannelNumber(singleplot, I32T(state->surfaceMap->sizeX),
		I32T(state->surfaceMap->sizeY), 3, frameOut);
	// Validate frame.
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

	// Free maps.
	simple2DBufferFreeFloat(state->surfaceMap);
	simple2DBufferFreeLong(state->surfaceMapLastTs);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);
}

static void caerSpikeFeaturesReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	SFFilterState state = moduleData->moduleState;

	state->lastTimeStamp = 0;

	// Reset maps to all zeros (startup state).
	simple2DBufferResetFloat(state->surfaceMap);
	simple2DBufferResetLong(state->surfaceMapLastTs);
}
