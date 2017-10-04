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
	simple2DBufferLong outputFrame; // Image matrix.
	int32_t numSpikes; // After how many spikes will we generate an image.
	int32_t spikeCounter; // Actual number of spikes seen so far, in range [0, numSpikes].
	float resolutionX;
	float resolutionY;
};

typedef struct imagegenerator_state *imagegeneratorState;

static bool caerImageGeneratorInit(caerModuleData moduleData);
static void caerImageGeneratorRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerImageGeneratorExit(caerModuleData moduleData);
static void caerImageGeneratorConfig(caerModuleData moduleData);
static void normalize_image_map_sigma(imagegeneratorState state);

static struct caer_module_functions caerImageGeneratorFunctions = { .moduleInit = &caerImageGeneratorInit, .moduleRun =
	&caerImageGeneratorRun, .moduleConfig = &caerImageGeneratorConfig, .moduleExit = &caerImageGeneratorExit };

static const struct caer_event_stream_in moduleInputs[] = { { .type = POLARITY_EVENT, .number = 1, .readOnly = true }, };

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "ImageGenerator", .description =
	"Generate a NxM frame from accumulating events over time.", .type = CAER_MODULE_PROCESSOR, .memSize =
	sizeof(struct imagegenerator_state), .functions = &caerImageGeneratorFunctions, .inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs), .outputStreams = moduleOutputs, .outputStreamsSize =
		CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs) };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

static bool caerImageGeneratorInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateInt(moduleData->moduleNode, "numSpikes", 2000, 1, 1000000, SSHS_FLAGS_NORMAL,
		"Number of spikes to accumulate.");
	sshsNodeCreateBool(moduleData->moduleNode, "rectifyPolarities", true, SSHS_FLAGS_NORMAL,
		"Consider ON/OFF polarities the same.");
	sshsNodeCreateShort(moduleData->moduleNode, "colorScale", 200, 1, 255, SSHS_FLAGS_NORMAL, "Color scale.");
	sshsNodeCreateShort(moduleData->moduleNode, "outputFrameSizeX", 32, 1, 1024, SSHS_FLAGS_NORMAL,
		"Output frame width. Must restart to take effect.");
	sshsNodeCreateShort(moduleData->moduleNode, "outputFrameSizeY", 32, 1, 1024, SSHS_FLAGS_NORMAL,
		"Output frame height. Must restart to take effect.");

	imagegeneratorState state = moduleData->moduleState;

	// Wait for source size information to be available.
	// Allocate map using info from sourceInfo.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	int16_t polaritySizeX = sshsNodeGetShort(sourceInfo, "polaritySizeX");
	int16_t polaritySizeY = sshsNodeGetShort(sourceInfo, "polaritySizeY");

	int16_t outputFrameSizeX = sshsNodeGetShort(moduleData->moduleNode, "outputFrameSizeX");
	int16_t outputFrameSizeY = sshsNodeGetShort(moduleData->moduleNode, "outputFrameSizeY");


	// Allocate map, sizes are known.
	state->outputFrame = simple2DBufferInitLong((size_t) outputFrameSizeX, (size_t) outputFrameSizeY);
	if (state->outputFrame == NULL) {
		return (false);
	}

	state->resolutionX = (float) state->outputFrame->sizeX / (float) polaritySizeX;
	state->resolutionY = (float) state->outputFrame->sizeY / (float) polaritySizeY;

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", outputFrameSizeX, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output frame width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", outputFrameSizeY, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output frame height.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", outputFrameSizeX, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", outputFrameSizeY, 1, 1024,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Output data height.");

	caerImageGeneratorConfig(moduleData);

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
	sshsNodeClearSubTree(sourceInfoNode, true);

	imagegeneratorState state = moduleData->moduleState;

	// Ensure map is freed.
	simple2DBufferFreeLong(state->outputFrame);
}

// This function implements 3sigma normalization and converts the image in nullhop format
static void normalize_image_map_sigma(imagegeneratorState state) {

	long sum = 0, count = 0;
	for (size_t x = 0; x < state->outputFrame->sizeX; x++) {
		for (size_t y = 0; y < state->outputFrame->sizeY; y++) {
			if (state->outputFrame->buffer2d[x][y] != 0) {
				sum += state->outputFrame->buffer2d[x][y];
				count++;
			}
		}
	}

	float mean = (float) sum / (float) count;

	float var = 0;
	for (size_t x = 0; x < state->outputFrame->sizeX; x++) {
		for (size_t y = 0; y < state->outputFrame->sizeY; y++) {
			if (state->outputFrame->buffer2d[x][y] != 0) {
				float f = (float) state->outputFrame->buffer2d[x][y] - mean;
				var += f * f;
			}
		}
	}

	float sig = sqrtf(var / (float) count);
	if (sig < (0.1f / 255.0f)) {
		sig = 0.1f / 255.0f;
	}

	float numSDevs = 3;
	float mean_png_gray, range, halfrange;

	if (state->rectifyPolarities) {
		mean_png_gray = 0; // rectified
	}
	else {
		mean_png_gray = 127.5f;
	}

	if (state->rectifyPolarities) {
		range = numSDevs * sig * (1.0f / 255.0f);
		halfrange = 0;
	}
	else {
		range = numSDevs * sig * 2 * (1.0f / 255.0f);
		halfrange = numSDevs * sig;
	}

	for (size_t x = 0; x < state->outputFrame->sizeX; x++) {
		for (size_t y = 0; y < state->outputFrame->sizeY; y++) {
			if (state->outputFrame->buffer2d[x][y] == 0) {
				state->outputFrame->buffer2d[x][y] = U16T(mean_png_gray);
			}
			else {
				float f = ((float) state->outputFrame->buffer2d[x][y] + halfrange) / range;

				if (f > 255.0f) {
					f = 255.0f;
				}
				else if (f < 0) {
					f = 0;
				}

				state->outputFrame->buffer2d[x][y] = U16T(floorf(f));
			}
		}
	}
}

static void caerImageGeneratorRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerPolarityEventPacketConst polarity =
		(caerPolarityEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	imagegeneratorState state = moduleData->moduleState;

	int32_t counterFrame = 0;

	CAER_POLARITY_CONST_ITERATOR_VALID_START(polarity)
		// Get coordinates and polarity (0 or 1) of latest spike.
		uint16_t polarityX = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t polarityY = caerPolarityEventGetY(caerPolarityIteratorElement);
		bool pol = caerPolarityEventGetPolarity(caerPolarityIteratorElement);

		uint16_t pos_x = U16T(floorf(state->resolutionX * (float ) polarityX));
		uint16_t pos_y = U16T(floorf(state->resolutionY * (float ) polarityY));

		// Update image map.
		if (state->rectifyPolarities) {
			// Rectify events, both polarities are considered to be the same.
			state->outputFrame->buffer2d[pos_x][pos_y] = state->outputFrame->buffer2d[pos_x][pos_y] + 1;
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
			// Normalize image.
			normalize_image_map_sigma(state);

			// Generate image.
			caerFrameEventPacket frameOut = NULL;

			if (*out == NULL) {
				// Allocate packet container for result packet.
				*out = caerEventPacketContainerAllocate(1);
				if (*out == NULL) {
					return; // Error.
				}

				int32_t numMaxFrames = (caerEventPacketHeaderGetEventValid(&polarity->packetHeader) / state->numSpikes)
					+ 1;

				// everything that is in the out packet container will be automatically be free after main loop
				frameOut = caerFrameEventPacketAllocate(numMaxFrames, moduleData->moduleID,
					caerEventPacketHeaderGetEventTSOverflow(&polarity->packetHeader), I32T(state->outputFrame->sizeX),
					I32T(state->outputFrame->sizeY), GRAYSCALE);
				if (frameOut == NULL) {
					return; // Error.
				}
				else {
					// Add output packet to packet container.
					caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) frameOut);
				}
			}
			else {
				frameOut = (caerFrameEventPacket) caerEventPacketContainerGetEventPacket(*out, 0);
			}

			caerFrameEvent singleplot = caerFrameEventPacketGetEvent(frameOut, counterFrame++);

			uint32_t counter = 0;
			for (size_t y = 0; y < state->outputFrame->sizeY; y++) {
				for (size_t x = 0; x < state->outputFrame->sizeX; x++) {
					singleplot->pixels[counter] = U16T(state->outputFrame->buffer2d[x][y] * 256); // grayscale
					counter += GRAYSCALE;
				}
			}

			caerFrameEventSetLengthXLengthYChannelNumber(singleplot, I32T(state->outputFrame->sizeX),
				I32T(state->outputFrame->sizeY), GRAYSCALE, frameOut);
			caerFrameEventValidate(singleplot, frameOut);

			// reset values
			simple2DBufferResetLong(state->outputFrame);

			state->spikeCounter = 0;
		}
	}
}
