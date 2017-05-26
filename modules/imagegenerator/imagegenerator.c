/**
 *  Accumulates a fixed number of events and generates frames.
 *  Author: federico.corradi@inilabs.com
 */
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include <math.h>

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

struct imagegenerator_state {
	bool rectifyPolarities;
	uint8_t colorScale;
	// Image matrix.
	simple2DBufferLong outputFrame;
	int32_t numSpikes; // After how many spikes will we generate an image.
	int32_t spikeCounter; // Actual number of spikes seen so far, in range [0, numSpikes].
};

typedef struct imagegenerator_state *imagegeneratorState;

static bool caerImageGeneratorInit(caerModuleData moduleData);
static void caerImageGeneratorRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerImageGeneratorExit(caerModuleData moduleData);
static void caerImageGeneratorConfig(caerModuleData moduleData);

static struct caer_module_functions caerImageGeneratorFunctions = { .moduleInit = &caerImageGeneratorInit, .moduleRun =
	&caerImageGeneratorRun, .moduleConfig = &caerImageGeneratorConfig, .moduleExit = &caerImageGeneratorExit };

static const struct caer_event_stream_in moduleInputs[] = { { .type = POLARITY_EVENT, .number = 1, .readOnly = true }, };

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "ImageGenerator", .type =
	CAER_MODULE_PROCESSOR, .memSize = sizeof(struct imagegenerator_state), .functions = &caerImageGeneratorFunctions,
	.inputStreams = moduleInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs), .outputStreams =
		moduleOutputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs) };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

static bool caerImageGeneratorInit(caerModuleData moduleData) {
	imagegeneratorState state = moduleData->moduleState;

	sshsNodeCreateInt(moduleData->moduleNode, "numSpikes", 2000, 0, 200000, SSHS_FLAGS_NORMAL,
		"Number of spikes to accumulate.");
	sshsNodeCreateBool(moduleData->moduleNode, "rectifyPolarities", true, SSHS_FLAGS_NORMAL,
		"Consider ON/OFF polarities the same.");
	sshsNodeCreateShort(moduleData->moduleNode, "colorScale", 200, 0, 255, SSHS_FLAGS_NORMAL, "Color scale.");
	sshsNodeCreateShort(moduleData->moduleNode, "outputFrameSizeX", 32, 1, 1024, SSHS_FLAGS_NORMAL,
		"Output frame width.");
	sshsNodeCreateShort(moduleData->moduleNode, "outputFrameSizeY", 32, 1, 1024, SSHS_FLAGS_NORMAL,
		"Output frame height.");

	state->numSpikes = sshsNodeGetInt(moduleData->moduleNode, "numSpikes");
	state->rectifyPolarities = sshsNodeGetBool(moduleData->moduleNode, "rectifyPolarities");
	state->colorScale = U8T(sshsNodeGetShort(moduleData->moduleNode, "colorScale"));

	int16_t outputFrameSizeX = sshsNodeGetShort(moduleData->moduleNode, "outputFrameSizeX");
	int16_t outputFrameSizeY = sshsNodeGetShort(moduleData->moduleNode, "outputFrameSizeY");

	// Allocate map, sizes are known.
	state->outputFrame = simple2DBufferInitLong((size_t) outputFrameSizeX, (size_t) outputFrameSizeY);
	if (state->outputFrame == NULL) {
		return (false);
	}

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", outputFrameSizeX, 1, 1024,
		SSHS_FLAGS_READ_ONLY_FORCE_DEFAULT_VALUE, "Data output width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", outputFrameSizeY, 1, 1024,
		SSHS_FLAGS_READ_ONLY_FORCE_DEFAULT_VALUE, "Data output height.");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	return (true);
}

static void caerImageGeneratorConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	imagegeneratorState state = moduleData->moduleState;

	state->numSpikes = sshsNodeGetInt(moduleData->moduleNode, "numSpikes");
	state->rectifyPolarities = sshsNodeGetBool(moduleData->moduleNode, "rectifyPolarities");
	state->colorScale = U8T(sshsNodeGetShort(moduleData->moduleNode, "colorScale"));
}

static void caerImageGeneratorExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);

	imagegeneratorState state = moduleData->moduleState;

	// Ensure map is freed.
	simple2DBufferFreeLong(state->outputFrame);
}

static void caerImageGeneratorRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerPolarityEventPacketConst polarity =
		(caerPolarityEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	// Wait for source size information to be available.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(caerEventPacketHeaderGetEventSource(&polarity->packetHeader));
	if (sourceInfo == NULL) {
		return;
	}

	float cam_sizeX = sshsNodeGetShort(sourceInfo, "dvsSizeX");
	float cam_sizeY = sshsNodeGetShort(sourceInfo, "dvsSizeY");

	imagegeneratorState state = moduleData->moduleState;

	float res_x = (float) state->outputFrame->sizeX / cam_sizeX;
	float res_y = (float) state->outputFrame->sizeY / cam_sizeY;

	int32_t numtotevs = caerEventPacketHeaderGetEventValid(&polarity->packetHeader);

	// Default is all events
	int numevs_start = 0;
	int numevs_end = numtotevs;

	if (numtotevs >= state->numSpikes) {
		//get rid of accumulated spikes
		simple2DBufferResetLong(state->outputFrame);

		state->spikeCounter = 0;
		//takes only the last 2000
		numevs_start = numtotevs - state->numSpikes;
		numevs_end = numtotevs;

	}
	else if ((numtotevs + state->spikeCounter) >= state->numSpikes) {
		//takes only the last 2000
		numevs_start = numtotevs - (state->numSpikes - state->spikeCounter);
		numevs_end = numtotevs;
	}

	for (int32_t caerPolarityIteratorCounter = numevs_start; caerPolarityIteratorCounter < numevs_end;
		caerPolarityIteratorCounter++) {
		caerPolarityEventConst caerPolarityIteratorElement = caerPolarityEventPacketGetEventConst(polarity,
			caerPolarityIteratorCounter);

		if (!caerPolarityEventIsValid(caerPolarityIteratorElement)) {
			continue;
		} // Skip invalid polarity events.

		// Get coordinates and polarity (0 or 1) of latest spike.
		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);
		bool pol = caerPolarityEventGetPolarity(caerPolarityIteratorElement);

		uint16_t pos_x = U16T(floorf(res_x * (float ) x));
		uint16_t pos_y = U16T(floorf(res_y * (float ) y));

		// Update image Map
		if (state->rectifyPolarities) {
			state->outputFrame->buffer2d[pos_x][pos_y] = state->outputFrame->buffer2d[pos_x][pos_y] + 1; //rectify events
		}
		else {
			if (pol) {
				state->outputFrame->buffer2d[pos_x][pos_y] = state->outputFrame->buffer2d[pos_x][pos_y] + 1;
			}
			else {
				state->outputFrame->buffer2d[pos_x][pos_y] = state->outputFrame->buffer2d[pos_x][pos_y] - 1;
			}
		}

		if (state->outputFrame->buffer2d[pos_x][pos_y] > state->colorScale) {
			state->outputFrame->buffer2d[pos_x][pos_y] = state->colorScale;
		}
		else if (state->outputFrame->buffer2d[pos_x][pos_y] < -state->colorScale) {
			state->outputFrame->buffer2d[pos_x][pos_y] = -state->colorScale;
		}

		state->spikeCounter += 1;

		// If we saw enough spikes, generate Image from ImageMap.
		if (state->spikeCounter >= state->numSpikes) {
			// reset values
			simple2DBufferResetLong(state->outputFrame);

			state->spikeCounter = 0;
		}
	}

	// Generate output frame.
	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
		return; // Error.
	}

	// everything that is in the out packet container will be automatically be free after main loop
	caerFrameEventPacket frameOut = caerFrameEventPacketAllocate(1, moduleData->moduleID,
		caerEventPacketHeaderGetEventTSOverflow(&polarity->packetHeader), I32T(state->outputFrame->sizeX),
		I32T(state->outputFrame->sizeY), 3);
	if (frameOut == NULL) {
		return; // Error.
	}
	else {
		// Add output packet to packet container.
		caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) frameOut);
	}

	caerFrameEvent singleplot = caerFrameEventPacketGetEvent(frameOut, 0);

	uint32_t counter = 0;
	for (size_t x = 0; x < state->outputFrame->sizeX; x++) {
		for (size_t y = 0; y < state->outputFrame->sizeY; y++) {
			singleplot->pixels[counter] = U16T(state->outputFrame->buffer2d[x][y] * 14); // red
			singleplot->pixels[counter + 1] = U16T(state->outputFrame->buffer2d[x][y] * 14); // green
			singleplot->pixels[counter + 2] = U16T(state->outputFrame->buffer2d[x][y] * 14); // blue
			counter += 3;
		}
	}

	caerFrameEventSetLengthXLengthYChannelNumber(singleplot, I32T(state->outputFrame->sizeX),
		I32T(state->outputFrame->sizeY), 3, frameOut);
	caerFrameEventValidate(singleplot, frameOut);
}
