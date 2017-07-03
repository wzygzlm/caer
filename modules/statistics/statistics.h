#ifndef STATISTICS_H_
#define STATISTICS_H_

#include "main.h"

#include <libcaer/events/common.h>
#include <time.h>
#include <sys/time.h>
#include "ext/portable_time.h"

#define CAER_STATISTICS_STRING_TOTAL "Total events/second: %10" PRIu64
#define CAER_STATISTICS_STRING_VALID "Valid events/second: %10" PRIu64

struct caer_statistics_state {
	uint64_t divisionFactor;
	char *currentStatisticsStringTotal;
	char *currentStatisticsStringValid;
	// Internal book-keeping.
	struct timespec lastTime;
	uint64_t totalEventsCounter;
	uint64_t validEventsCounter;
};

typedef struct caer_statistics_state *caerStatisticsState;

// For reuse inside other modules.
static inline bool caerStatisticsStringInit(caerStatisticsState state) {
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

static inline void caerStatisticsStringUpdate(caerEventPacketHeaderConst packetHeader, caerStatisticsState state) {
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

static inline void caerStatisticsStringExit(caerStatisticsState state) {
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

static inline void caerStatisticsStringReset(caerStatisticsState state) {
	// Reset counters.
	state->totalEventsCounter = 0;
	state->validEventsCounter = 0;

	// Update to current time.
	portable_clock_gettime_monotonic(&state->lastTime);
}

#endif /* STATISTICS_H_ */
