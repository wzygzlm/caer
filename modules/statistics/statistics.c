#include "statistics.h"
#include "base/mainloop.h"
#include "base/module.h"

static bool caerStatisticsInit(caerModuleData moduleData);
static void caerStatisticsRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerStatisticsExit(caerModuleData moduleData);
static void caerStatisticsReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions StatisticsFunctions = { .moduleInit = &caerStatisticsInit, .moduleRun =
	&caerStatisticsRun, .moduleConfig = NULL, .moduleExit = &caerStatisticsExit, .moduleReset = &caerStatisticsReset };

static const struct caer_event_stream_in StatisticsInputs[] = { { .type = -1, .number = 1, .readOnly = true } };

static const struct caer_module_info StatisticsInfo = { .version = 1, .name = "Statistics", .description =
	"Display statistics on number of events.", .type = CAER_MODULE_OUTPUT, .memSize =
	sizeof(struct caer_statistics_state), .functions = &StatisticsFunctions, .inputStreams = StatisticsInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(StatisticsInputs), .outputStreams =
	NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&StatisticsInfo);
}

static bool caerStatisticsInit(caerModuleData moduleData) {
	caerStatisticsState state = moduleData->moduleState;

	// Configurable division factor.
	sshsNodeCreateLong(moduleData->moduleNode, "divisionFactor", 1000, 1, INT64_MAX, SSHS_FLAGS_NORMAL,
		"Division factor for statistics display, to get Kilo/Mega/... events shown.");
	state->divisionFactor = U64T(sshsNodeGetLong(moduleData->moduleNode, "divisionFactor"));

	return (caerStatisticsStringInit(state));
}

static void caerStatisticsRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
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

static void caerStatisticsReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	caerStatisticsState state = moduleData->moduleState;

	caerStatisticsStringReset(state);
}
