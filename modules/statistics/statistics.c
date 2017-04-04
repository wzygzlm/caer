#include "statistics.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/portable_time.h"

static bool caerStatisticsInit(caerModuleData moduleData);
static void caerStatisticsRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerStatisticsExit(caerModuleData moduleData);
static void caerStatisticsReset(caerModuleData moduleData, uint16_t resetCallSourceID);

static struct caer_module_functions StatisticsFunctions = { .moduleInit = &caerStatisticsInit, .moduleRun =
	&caerStatisticsRun, .moduleConfig = NULL, .moduleExit = &caerStatisticsExit, .moduleReset = &caerStatisticsReset };

static const struct caer_event_stream StatisticsInputs[] = { { .type = -1, .number = 1 } };

static const struct caer_module_info StatisticsInfo = { .version = 1, .name = "Statistics", .type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct caer_statistics_state), .functions = &StatisticsFunctions, .inputStreams = StatisticsInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_SIZE(StatisticsInputs), .outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&StatisticsInfo);
}

static bool caerStatisticsInit(caerModuleData moduleData) {
	caerStatisticsState state = moduleData->moduleState;

	// Configurable division factor.
	sshsNodeCreateLong(moduleData->moduleNode, "divisionFactor", 1000, 1, INT64_MAX, SSHS_FLAGS_NORMAL);
	state->divisionFactor = U64T(sshsNodeGetLong(moduleData->moduleNode, "divisionFactor"));

	return (caerStatisticsStringInit(state));
}

static void caerStatisticsRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	// Interpret variable arguments (same as above in main function).
	caerEventPacketHeaderConst packetHeader = caerEventPacketContainerGetEventPacketConst(in, 0);

	caerStatisticsState state = moduleData->moduleState;
	caerStatisticsStringUpdate(packetHeader, state);

	fprintf(stdout, "\r%s - %s", state->currentStatisticsStringTotal, state->currentStatisticsStringValid);
	fflush(stdout);
}

static void caerStatisticsExit(caerModuleData moduleData) {
	caerStatisticsState state = moduleData->moduleState;

	caerStatisticsStringExit(state);
}

static void caerStatisticsReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	caerStatisticsState state = moduleData->moduleState;

	caerStatisticsStringReset(state);
}

bool caerStatisticsStringInit(caerStatisticsState state) {
	// Total and Valid parts have same length.
	size_t maxSplitStatStringLength = (size_t) snprintf(NULL, 0, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);

	state->currentStatisticsStringTotal = calloc(maxSplitStatStringLength + 1, sizeof(char)); // +1 for NUL termination.
	if (state->currentStatisticsStringTotal == NULL) {
		return (false);
	}

	state->currentStatisticsStringValid = calloc(maxSplitStatStringLength + 1, sizeof(char)); // +1 for NUL termination.
	if (state->currentStatisticsStringValid == NULL) {
		free(state->currentStatisticsStringTotal);
		state->currentStatisticsStringTotal = NULL;

		return (false);
	}

	// Initialize to current time.
	portable_clock_gettime_monotonic(&state->lastTime);

	// Set division factor to 1 by default (avoid division by zero).
	state->divisionFactor = 1;

	return (true);
}

void caerStatisticsStringUpdate(caerEventPacketHeaderConst packetHeader, caerStatisticsState state) {
	// Only non-NULL packets (with content!) contribute to the event count.
	if (packetHeader != NULL) {
		state->totalEventsCounter += U64T(caerEventPacketHeaderGetEventNumber(packetHeader));
		state->validEventsCounter += U64T(caerEventPacketHeaderGetEventValid(packetHeader));
	}

	// Print up-to-date statistic roughly every second, taking into account possible deviations.
	struct timespec currentTime;
	portable_clock_gettime_monotonic(&currentTime);

	uint64_t diffNanoTime = (uint64_t) (((int64_t) (currentTime.tv_sec - state->lastTime.tv_sec) * 1000000000LL)
		+ (int64_t) (currentTime.tv_nsec - state->lastTime.tv_nsec));

	// DiffNanoTime is the difference in nanoseconds; we want to trigger roughly every second.
	if (diffNanoTime >= 1000000000LLU) {
		// Print current values.
		uint64_t totalEventsPerTime = (state->totalEventsCounter * (1000000000LLU / state->divisionFactor))
			/ diffNanoTime;
		uint64_t validEventsPerTime = (state->validEventsCounter * (1000000000LLU / state->divisionFactor))
			/ diffNanoTime;

		sprintf(state->currentStatisticsStringTotal, CAER_STATISTICS_STRING_TOTAL, totalEventsPerTime);
		sprintf(state->currentStatisticsStringValid, CAER_STATISTICS_STRING_VALID, validEventsPerTime);

		// Reset for next update.
		state->totalEventsCounter = 0;
		state->validEventsCounter = 0;
		state->lastTime = currentTime;
	}
}

void caerStatisticsStringExit(caerStatisticsState state) {
	// Reclaim string memory.
	if (state->currentStatisticsStringTotal != NULL) {
		free(state->currentStatisticsStringTotal);
		state->currentStatisticsStringTotal = NULL;
	}

	if (state->currentStatisticsStringValid != NULL) {
		free(state->currentStatisticsStringValid);
		state->currentStatisticsStringValid = NULL;
	}
}

void caerStatisticsStringReset(caerStatisticsState state) {
	// Reset counters.
	state->totalEventsCounter = 0;
	state->validEventsCounter = 0;

	// Update to current time.
	portable_clock_gettime_monotonic(&state->lastTime);
}
