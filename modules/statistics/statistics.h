#ifndef STATISTICS_H_
#define STATISTICS_H_

#include "main.h"

#include <libcaer/events/common.h>
#include <time.h>
#include <sys/time.h>
#include "ext/portable_time.h"

#define CAER_STATISTICS_STRING_TOTAL "Total events/second: %10" PRIu64
#define CAER_STATISTICS_STRING_VALID "Valid events/second: %10" PRIu64
#define CAER_STATISTICS_STRING_USBTDIFF "Max packets time diff us: %10" PRIu64

struct caer_statistics_state {
	uint64_t divisionFactor;
	char *currentStatisticsStringTotal;
	char *currentStatisticsStringValid;
	char *currentStatisticsStringGap;
	// Internal book-keeping.
	struct timespec lastTime;
	uint64_t totalEventsCounter;
	uint64_t validEventsCounter;
	int32_t maxTimeGap;
	int32_t lastTs;
};

typedef struct caer_statistics_state *caerStatisticsState;

// For reuse inside other modules.
static inline bool caerStatisticsStringInit(caerStatisticsState state) {
	// Total and Valid parts have same length.
	size_t maxSplitStatStringLength = (size_t) snprintf(NULL, 0, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);

	state->currentStatisticsStringTotal = (char *) calloc(maxSplitStatStringLength + 1, sizeof(char)); // +1 for NUL termination.
	if (state->currentStatisticsStringTotal == NULL) {
		return (false);
	}

	state->currentStatisticsStringValid = (char *) calloc(maxSplitStatStringLength + 1, sizeof(char)); // +1 for NUL termination.
	if (state->currentStatisticsStringValid == NULL) {
		free(state->currentStatisticsStringTotal);
		state->currentStatisticsStringTotal = NULL;

		return (false);
	}

	state->currentStatisticsStringGap = (char *) calloc(maxSplitStatStringLength + 1, sizeof(char)); // +1 for NUL termination.
	if (state->currentStatisticsStringGap == NULL) {
		free(state->currentStatisticsStringTotal);
		state->currentStatisticsStringTotal = NULL;

		free(state->currentStatisticsStringValid);
		state->currentStatisticsStringValid = NULL;

		return (false);
	}

	// Initialize to current time.
	portable_clock_gettime_monotonic(&state->lastTime);

	// Set division factor to 1 by default (avoid division by zero).
	state->divisionFactor = 1;

	// Last packet timestamp, zero init
	state->lastTs = 0;
	state->maxTimeGap = 0;

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

	// Calculate packets time gap
	if (packetHeader != NULL) {
		int32_t evNumber = caerEventPacketHeaderGetEventNumber(packetHeader);
		const void * event_last = caerGenericEventGetEvent(packetHeader, evNumber - 1); // last event
		const void * event_first = caerGenericEventGetEvent(packetHeader, 0);			// first event
		int32_t tspacket_last = caerGenericEventGetTimestamp(event_last, packetHeader);
		int32_t tspacket_first = caerGenericEventGetTimestamp(event_first, packetHeader);
		int32_t gap = 0;

		if (state->lastTs == 0) {
			state->lastTs = tspacket_last;
		}
		else {
			gap = tspacket_first - state->lastTs;
			state->lastTs = tspacket_last;
		}

		if (gap > state->maxTimeGap) {
			state->maxTimeGap = gap;
		}
	}

	// DiffNanoTime is the difference in nanoseconds; we want to trigger roughly every second.
	if (diffNanoTime >= 1000000000LLU) {
		// Print current values.
		uint64_t totalEventsPerTime = (state->totalEventsCounter * (1000000000LLU / state->divisionFactor))
			/ diffNanoTime;
		uint64_t validEventsPerTime = (state->validEventsCounter * (1000000000LLU / state->divisionFactor))
			/ diffNanoTime;

		uint64_t gapTime = (uint64_t) (state->maxTimeGap);

		sprintf(state->currentStatisticsStringTotal, CAER_STATISTICS_STRING_TOTAL, totalEventsPerTime);
		sprintf(state->currentStatisticsStringValid, CAER_STATISTICS_STRING_VALID, validEventsPerTime);
		sprintf(state->currentStatisticsStringGap, CAER_STATISTICS_STRING_USBTDIFF, gapTime);

		// Reset for next update.
		state->totalEventsCounter = 0;
		state->validEventsCounter = 0;
		state->lastTime = currentTime;
		state->maxTimeGap = 0;
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

	if (state->currentStatisticsStringGap != NULL) {
		free(state->currentStatisticsStringGap);
		state->currentStatisticsStringGap = NULL;
	}
}

static inline void caerStatisticsStringReset(caerStatisticsState state) {
	// Reset counters.
	state->totalEventsCounter = 0;
	state->validEventsCounter = 0;
	state->lastTs = 0;
	state->maxTimeGap = 0;

	// Update to current time.
	portable_clock_gettime_monotonic(&state->lastTime);
}

#endif /* STATISTICS_H_ */
