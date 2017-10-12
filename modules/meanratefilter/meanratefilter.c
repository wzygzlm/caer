/*
 *
 *  Created on: Dec, 2016
 *      Author: federico.corradi@inilabs.com
 */

#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include "ext/portable_time.h"
#include "ext/colorjet/colorjet.h"
#include <time.h>
#include <libcaer/devices/dynapse.h>
#include <libcaer/events/spike.h>
#include <libcaer/events/frame.h> //display
#include "modules/ini/dynapse_utils.h"

struct MRFilter_state {
	sshsNode dynapseConfigNode;
	simple2DBufferFloat frequencyMap;
	simple2DBufferLong spikeCountMap;
	int32_t colorscaleMax;
	int32_t colorscaleMin;
	float targetFreq;
	float measureMinTime;
	bool doSetFreq;
	bool startedMeasure;
	double measureStartedAt;
};

typedef struct MRFilter_state *MRFilterState;

static bool caerMeanRateFilterInit(caerModuleData moduleData);
static void caerMeanRateFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerMeanRateFilterConfig(caerModuleData moduleData);
static void caerMeanRateFilterExit(caerModuleData moduleData);
static void caerMeanRateFilterReset(caerModuleData moduleData, int16_t resetCallSourceID);
static void generateOutputFrame(caerEventPacketContainer *out, MRFilterState state, int16_t moduleId,
	int32_t tsOverflow);

static struct caer_module_functions caerMeanRateFilterFunctions = { .moduleInit = &caerMeanRateFilterInit, .moduleRun =
	&caerMeanRateFilterRun, .moduleConfig = &caerMeanRateFilterConfig, .moduleExit = &caerMeanRateFilterExit,
	.moduleReset = &caerMeanRateFilterReset };

static const struct caer_event_stream_in moduleInputs[] = { { .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_event_stream_out moduleOutputs[] = { { .type = FRAME_EVENT } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "MeanRate", .description =
	"Measure mean rate activity of neurons and adjust chip biases accordingly.", .type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct MRFilter_state), .functions = &caerMeanRateFilterFunctions, .inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs), .outputStreams = moduleOutputs, .outputStreamsSize =
		CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs) };

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

	int16_t sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateInt(moduleData->moduleNode, "colorscaleMax", 500, 0, 1000, SSHS_FLAGS_NORMAL,
		"Color Scale, i.e. Max Frequency (Hz).");
	sshsNodeCreateInt(moduleData->moduleNode, "colorscaleMin", 0, 0, 1000, SSHS_FLAGS_NORMAL,
		"Color Scale, i.e. Min Frequency (Hz).");
	sshsNodeCreateFloat(moduleData->moduleNode, "targetFreq", 100, 0, 250, SSHS_FLAGS_NORMAL,
		"Target frequency for neurons.");
	sshsNodeCreateFloat(moduleData->moduleNode, "measureMinTime", 3, 0.001f, 300, SSHS_FLAGS_NORMAL,
		"Measure time before updating the mean (in seconds).");
	sshsNodeCreateBool(moduleData->moduleNode, "doSetFreq", false, SSHS_FLAGS_NORMAL,
		"Start/Stop changing biases for reaching target frequency.");

	sshsNode sourceInfoSource = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfoSource == NULL) {
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoSource, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoSource, "dataSizeY");

	state->frequencyMap = simple2DBufferInitFloat((size_t) sizeX, (size_t) sizeY);
	if (state->frequencyMap == NULL) {
		return (false);
	}

	state->spikeCountMap = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->spikeCountMap == NULL) {
		return (false);
	}

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeX", sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output frame width.");
	sshsNodeCreateShort(sourceInfoNode, "frameSizeY", sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output frame height.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", sizeX, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output data width.");
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", sizeY, 1, 1024, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Output data height.");

	caerMeanRateFilterConfig(moduleData);

	state->dynapseConfigNode = caerMainloopGetSourceNode(sourceID);

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerMeanRateFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerSpikeEventPacketConst spike = (caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in,
		SPIKE_EVENT);

	// Only process packets with content.
	if (spike == NULL) {
		return;
	}

	MRFilterState state = moduleData->moduleState;

	// Iterate over events and update count
	CAER_SPIKE_CONST_ITERATOR_VALID_START(spike)
		uint16_t x = caerDynapseSpikeEventGetX(caerSpikeIteratorElement);
		uint16_t y = caerDynapseSpikeEventGetY(caerSpikeIteratorElement);

		state->spikeCountMap->buffer2d[x][y] += 1;
	CAER_SPIKE_ITERATOR_VALID_END

	// if not measuring, let's start
	if (!state->startedMeasure) {
		struct timespec tStart;
		portable_clock_gettime_monotonic(&tStart);
		state->measureStartedAt = (double) tStart.tv_sec + 1.0e-9 * (double) tStart.tv_nsec;
		state->startedMeasure = true;
	}

	// get current time
	struct timespec tEnd;
	portable_clock_gettime_monotonic(&tEnd);
	double now = (double) tEnd.tv_sec + 1.0e-9 * (double) tEnd.tv_nsec;

	// if we measured for enough time..
	if ((now - state->measureStartedAt) >= (double) state->measureMinTime) {
		state->startedMeasure = false;

		// update frequencyMap
		for (size_t x = 0; x < state->frequencyMap->sizeX; x++) {
			for (size_t y = 0; y < state->frequencyMap->sizeY; y++) {
				state->frequencyMap->buffer2d[x][y] = (float) state->spikeCountMap->buffer2d[x][y]
					/ state->measureMinTime;

				// reset count
				state->spikeCountMap->buffer2d[x][y] = 0;
			}
		}

		// Generate output frame, after frequency map has been updated.
		generateOutputFrame(out, state, moduleData->moduleID,
			caerEventPacketHeaderGetEventTSOverflow(&spike->packetHeader));

		// set the biases if asked
		if (state->doSetFreq) {
			// collect data for all chips and cores
			float sum[DYNAPSE_X4BOARD_NUMCHIPS][DYNAPSE_CONFIG_NUMCORES] = { 0 };
			float mean[DYNAPSE_X4BOARD_NUMCHIPS][DYNAPSE_CONFIG_NUMCORES] = { 0 };
			float var[DYNAPSE_X4BOARD_NUMCHIPS][DYNAPSE_CONFIG_NUMCORES] = { 0 };

			// loop over all chips and cores
			for (size_t chip = 0; chip < DYNAPSE_X4BOARD_NUMCHIPS; chip++) {
				for (size_t core = 0; core < DYNAPSE_CONFIG_NUMCORES; core++) {
					float maxFrequency = 0;

					// Calculate X/Y region of interest.
					size_t startX = ((core & 0x01) * DYNAPSE_CONFIG_NEUCOL)
						+ ((chip & 0x01) * DYNAPSE_CONFIG_XCHIPSIZE);
					size_t startY = ((core & 0x02) * DYNAPSE_CONFIG_NEUROW)
						+ ((chip & 0x02) * DYNAPSE_CONFIG_YCHIPSIZE);

					// get sum for core
					for (size_t x = startX; x < (startX + DYNAPSE_CONFIG_NEUCOL); x++) {
						for (size_t y = startY; y < (startY + DYNAPSE_CONFIG_NEUROW); y++) {
							sum[chip][core] += state->frequencyMap->buffer2d[x][y];

							if (maxFrequency < state->frequencyMap->buffer2d[x][y]) {
								maxFrequency = state->frequencyMap->buffer2d[x][y];
							}
						}
					}

					// calculate mean
					mean[chip][core] = sum[chip][core] / (float) DYNAPSE_CONFIG_NUMNEURONS_CORE;

					// calculate variance
					for (size_t x = startX; x < (startX + DYNAPSE_CONFIG_NEUCOL); x++) {
						for (size_t y = startY; y < (startY + DYNAPSE_CONFIG_NEUROW); y++) {
							float f = state->frequencyMap->buffer2d[x][y] - mean[chip][core];
							var[chip][core] += f * f;
						}
					}

					caerModuleLog(moduleData, CAER_LOG_NOTICE,
						"mean[%zu][%zu] = %f Hz var[%zu][%zu] = %f maxFrequency %f.", chip, core,
						(double) mean[chip][core], chip, core, (double) var[chip][core], (double) maxFrequency);
				}
			}

			// now decide how to change the bias setting
			for (uint8_t chip = 0; chip < DYNAPSE_X4BOARD_NUMCHIPS; chip++) {
				for (uint8_t core = 0; core < DYNAPSE_CONFIG_NUMCORES; core++) {
					caerModuleLog(moduleData, CAER_LOG_NOTICE,
						"mean[%d][%d] = %f Hz var[%d][%d] = %f chipId = %d coreId %d.", chip, core,
						(double) mean[chip][core], chip, core, (double) var[chip][core], chip, core);

					// current dc settings
					uint8_t coarseValue;
					uint8_t fineValue;
					caerDynapseGetBiasCore(state->dynapseConfigNode, chip, core, "IF_DC_P", &coarseValue, &fineValue,
						NULL);

					caerModuleLog(moduleData, CAER_LOG_NOTICE, "BIAS U%d C%d_IF_DC_P coarse %d fine %d.", chip, core,
						coarseValue, fineValue);

					bool changed = false;
					uint8_t step = 15; // fine step value

					// compare current frequency with target
					if ((state->targetFreq - mean[chip][core]) > 0) {
						// we need to increase freq -> increase fine
						if ((I16T(fineValue) + step) <= UINT8_MAX) {
							fineValue = U8T(fineValue + step);
							changed = true;
						}
						else {
							// if we did not reach the max value
							if (coarseValue != 0) {
								fineValue = step;
								coarseValue = U8T(coarseValue - 1); // coarse 0 is max 7 is min
								changed = true;
							}
							else {
								caerModuleLog(moduleData, CAER_LOG_NOTICE, "Reached Maximum Limit for Bias.");
							}
						}
					}
					else if ((state->targetFreq - mean[chip][core]) < 0) {
						// we need to reduce freq -> decrease fine
						if ((I16T(fineValue) - step) >= 0) {
							fineValue = U8T(fineValue - step);
							changed = true;
						}
						else {
							// if we did not reach the max value
							if (coarseValue != 7) {
								fineValue = step;
								coarseValue = U8T(coarseValue + 1); // coarse 0 is max 7 is min
								changed = true;
							}
							else {
								caerModuleLog(moduleData, CAER_LOG_NOTICE, "Reached Minimum Limit for Bias.");
							}
						}
					}

					if (changed) {
						// send new bias value
						caerDynapseSetBiasCore(state->dynapseConfigNode, chip, core, "IF_DC_P", coarseValue, fineValue,
							true);
					}
				}
			}
		}
	}
}

static void generateOutputFrame(caerEventPacketContainer *out, MRFilterState state, int16_t moduleId,
	int32_t tsOverflow) {
	// Allocate packet container for result packet.
	*out = caerEventPacketContainerAllocate(1);
	if (*out == NULL) {
		return; // Error.
	}

	// Everything that is in the out packet container will be automatically freed after main loop.
	caerFrameEventPacket frameOut = caerFrameEventPacketAllocate(1, moduleId, tsOverflow,
		I32T(state->frequencyMap->sizeX), I32T(state->frequencyMap->sizeY), RGB);
	if (frameOut == NULL) {
		return; // Error.
	}
	else {
		// Add output packet to packet container.
		caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) frameOut);
	}

	// Make image.
	caerFrameEvent frequencyPlot = caerFrameEventPacketGetEvent(frameOut, 0);

	size_t counter = 0;
	for (size_t y = 0; y < state->frequencyMap->sizeY; y++) {
		for (size_t x = 0; x < state->frequencyMap->sizeX; x++) {
			COLOUR color = GetColour(state->frequencyMap->buffer2d[x][y], state->colorscaleMin, state->colorscaleMax);
			frequencyPlot->pixels[counter] = U16T(color.r * UINT16_MAX); // red
			frequencyPlot->pixels[counter + 1] = U16T(color.g * UINT16_MAX); // green
			frequencyPlot->pixels[counter + 2] = U16T(color.b * UINT16_MAX); // blue
			counter += RGB;
		}
	}

	// Add info to frame.
	caerFrameEventSetLengthXLengthYChannelNumber(frequencyPlot, I32T(state->frequencyMap->sizeX),
		I32T(state->frequencyMap->sizeY), RGB, frameOut);

	// Validate frame.
	caerFrameEventValidate(frequencyPlot, frameOut);
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
	MRFilterState state = moduleData->moduleState;

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeClearSubTree(sourceInfoNode, true);

	// Ensure maps are freed.
	simple2DBufferFreeFloat(state->frequencyMap);
	simple2DBufferFreeLong(state->spikeCountMap);
}

static void caerMeanRateFilterReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	MRFilterState state = moduleData->moduleState;

	// Reset maps to all zeros (startup state).
	simple2DBufferResetFloat(state->frequencyMap);
	simple2DBufferResetLong(state->spikeCountMap);
}
