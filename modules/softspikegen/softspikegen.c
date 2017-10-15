#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/devices/dynapse.h>
#include <libcaer/events/spike.h>
#include "modules/ini/dynapse_utils.h"
#include "ext/portable_time.h"
#include "ext/c11threads_posix.h"
#include <math.h>

#define STIM_POISSON 	1
#define STIM_REGULAR 	2
#define STIM_GAUSSIAN 	3
#define STIM_PATTERNA   4
#define STIM_PATTERNB   5
#define STIM_PATTERNC   6
#define STIM_PATTERNA_SINGLE   7
#define STIM_PATTERNB_SINGLE   8
#define STIM_PATTERNC_SINGLE   9
#define STIM_PATTERND_SINGLE   10
#define STIM_ETF	11

struct gen_spike_state {
	caerDeviceHandle sourceDeviceHandle;
	sshsNode sourceConfigNode;
	atomic_bool doStim;
	atomic_int_fast32_t stim_type;
	atomic_int_fast32_t stim_avr; // Hertz [1/s]
	atomic_int_fast32_t stim_std;
	atomic_int_fast32_t stim_duration;
	atomic_bool repeat;
	atomic_bool teaching;
	atomic_bool sendTeachingStimuli;
	atomic_bool sendInhibitoryStimuli;
	atomic_bool setCam;
	atomic_bool setCamSingle;
	atomic_bool clearCam;
	atomic_bool clearAllCam;
	atomic_bool doStimPrimitiveBias;
	atomic_bool doStimPrimitiveCam;
	atomic_bool loadDefaultBiases;
	atomic_bool done;
	atomic_bool started;
	thrd_t spikeGenThread;
	atomic_bool running;
	/*address spike*/
	atomic_int_fast32_t core_d;
	atomic_int_fast32_t address;
	atomic_int_fast32_t core_s;
	atomic_int_fast32_t chip_id;
	atomic_int_fast32_t dx;
	atomic_int_fast32_t dy;
	atomic_bool sx;
	atomic_bool sy;
	/* ETF */
	// stimulation Thread ETF
	atomic_bool ETFstarted;
	atomic_bool ETFdone;
	atomic_int_fast32_t ETFchip_id;		// the chip that will be measured [0,4,8,12]
	atomic_int_fast32_t ETFduration;	// total stimulation duration
	atomic_int_fast32_t ETFphase_num;	// stimulation phase number
	atomic_bool ETFrepeat;
	int ETFstepnum;
};

typedef struct gen_spike_state *GenSpikeState;

static bool caerSoftSpikeGenModuleInit(caerModuleData moduleData);
static void caerSoftSpikeGenModuleExit(caerModuleData moduleData);

static struct caer_module_functions caerSoftSpikeGenModuleFunctions = { .moduleInit = &caerSoftSpikeGenModuleInit,
	.moduleRun = NULL, .moduleConfig = NULL, .moduleExit = &caerSoftSpikeGenModuleExit };

static const struct caer_event_stream_in moduleInputs[] = { { .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "SoftSpikeGen", .description =
	"Software Spike Generator", .type = CAER_MODULE_OUTPUT, .memSize = sizeof(struct gen_spike_state), .functions =
	&caerSoftSpikeGenModuleFunctions, .inputStreams = moduleInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(
	moduleInputs), .outputStreams = NULL, .outputStreamsSize = 0 };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

int spikeGenThread(void *spikeGenState);
void spiketrainETF(GenSpikeState spikeGenState);
void spiketrainReg(GenSpikeState spikeGenState);
void spiketrainPat(GenSpikeState spikeGenState,
	uint32_t spikePattern[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE]);
void spiketrainPatSingle(GenSpikeState spikeGenState, uint32_t sourceAddress);
void SetCam(GenSpikeState spikeGenState);
void SetCamSingle(GenSpikeState spikeGenState);
void ClearCam(GenSpikeState spikeGenState);
void ClearAllCam(GenSpikeState spikeGenState);
void ResetBiases(GenSpikeState spikeGenState);
static void spikeConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static struct timespec tstart = { 0, 0 }, tend = { 0, 0 };
static struct timespec tstart_etf = { 0, 0 }, tend_etf = { 0, 0 };
static int CamSeted = 0;
static int CamSetedSingle = 0;
static int CamCleared = 0;
static int CamAllCleared = 0;
static int BiasesLoaded = 0;
static int pattern_number = 4; // 3 or 4

bool caerSoftSpikeGenModuleInit(caerModuleData moduleData) {
	GenSpikeState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	state->sourceDeviceHandle = caerMainloopGetSourceState(sourceID);
	state->sourceConfigNode = caerMainloopGetSourceNode(sourceID);

	sshsNodeCreateBool(moduleData->moduleNode, "doStim", false, SSHS_FLAGS_NORMAL, "Enable stimulation.");
	atomic_store(&state->doStim, sshsNodeGetBool(moduleData->moduleNode, "doStim"));

	// TODO: fix range limits.
	sshsNodeCreateInt(moduleData->moduleNode, "stim_type", U8T(STIM_REGULAR), 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->stim_type, sshsNodeGetInt(moduleData->moduleNode, "stim_type"));

	sshsNodeCreateInt(moduleData->moduleNode, "stim_avr", 3, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->stim_avr, sshsNodeGetInt(moduleData->moduleNode, "stim_avr"));

	sshsNodeCreateInt(moduleData->moduleNode, "stim_std", 1, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->stim_std, sshsNodeGetInt(moduleData->moduleNode, "stim_std"));

	sshsNodeCreateInt(moduleData->moduleNode, "stim_duration", 10, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->stim_duration, sshsNodeGetInt(moduleData->moduleNode, "stim_duration"));

	sshsNodeCreateBool(moduleData->moduleNode, "repeat", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->repeat, sshsNodeGetBool(moduleData->moduleNode, "repeat"));

	sshsNodeCreateBool(moduleData->moduleNode, "teaching", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->teaching, sshsNodeGetBool(moduleData->moduleNode, "teaching"));

	sshsNodeCreateBool(moduleData->moduleNode, "sendTeachingStimuli", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->sendTeachingStimuli, sshsNodeGetBool(moduleData->moduleNode, "sendTeachingStimuli"));

	sshsNodeCreateBool(moduleData->moduleNode, "sendInhibitoryStimuli", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->sendInhibitoryStimuli, sshsNodeGetBool(moduleData->moduleNode, "sendInhibitoryStimuli"));

	sshsNodeCreateBool(moduleData->moduleNode, "setCam", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->setCam, sshsNodeGetBool(moduleData->moduleNode, "setCam"));

	sshsNodeCreateBool(moduleData->moduleNode, "setCamSingle", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->setCamSingle, sshsNodeGetBool(moduleData->moduleNode, "setCamSingle"));

	sshsNodeCreateBool(moduleData->moduleNode, "clearCam", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->clearCam, sshsNodeGetBool(moduleData->moduleNode, "clearCam"));

	sshsNodeCreateBool(moduleData->moduleNode, "clearAllCam", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->clearAllCam, sshsNodeGetBool(moduleData->moduleNode, "clearAllCam"));

	sshsNodeCreateBool(moduleData->moduleNode, "doStimPrimitiveBias", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->doStimPrimitiveBias, sshsNodeGetBool(moduleData->moduleNode, "doStimPrimitiveBias"));

	sshsNodeCreateBool(moduleData->moduleNode, "doStimPrimitiveCam", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->doStimPrimitiveCam, sshsNodeGetBool(moduleData->moduleNode, "doStimPrimitiveCam"));

	sshsNodeCreateBool(moduleData->moduleNode, "loadDefaultBiases", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->loadDefaultBiases, sshsNodeGetBool(moduleData->moduleNode, "loadDefaultBiases"));

	atomic_store(&state->started, false);
	atomic_store(&state->done, true);

	atomic_store(&state->ETFstarted, false);
	atomic_store(&state->ETFdone, false);
	atomic_store(&state->ETFchip_id, 0);
	atomic_store(&state->ETFduration, 30);
	atomic_store(&state->ETFphase_num, 0);
	atomic_store(&state->ETFrepeat, true);

	state->ETFstepnum = 6;	//internal

	// init status
	sshsNodeCreateBool(moduleData->moduleNode, "loadDefaultBiases", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->loadDefaultBiases, sshsNodeGetBool(moduleData->moduleNode, "loadDefaultBiases"));

	// Start separate stimulation thread.
	atomic_store(&state->running, true);

	if (thrd_create(&state->spikeGenThread, &spikeGenThread, state) != thrd_success) {
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "spikeGenThread: Failed to start thread.");
		return (NULL);
	}

	/*address*/
	sshsNodeCreateBool(moduleData->moduleNode, "sx", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->sx, sshsNodeGetBool(moduleData->moduleNode, "sx"));

	sshsNodeCreateBool(moduleData->moduleNode, "sy", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->sy, sshsNodeGetBool(moduleData->moduleNode, "sy"));

	sshsNodeCreateInt(moduleData->moduleNode, "core_d", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->core_d, sshsNodeGetInt(moduleData->moduleNode, "core_d"));

	sshsNodeCreateInt(moduleData->moduleNode, "core_s", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->core_s, sshsNodeGetInt(moduleData->moduleNode, "core_s"));

	sshsNodeCreateInt(moduleData->moduleNode, "address", 1, 0, INT32_MAX, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->address, sshsNodeGetInt(moduleData->moduleNode, "address"));

	sshsNodeCreateInt(moduleData->moduleNode, "dx", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->dx, sshsNodeGetInt(moduleData->moduleNode, "dx"));

	sshsNodeCreateInt(moduleData->moduleNode, "dy", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->dy, sshsNodeGetInt(moduleData->moduleNode, "dy"));

	sshsNodeCreateInt(moduleData->moduleNode, "chip_id", DYNAPSE_CONFIG_DYNAPSE_U0, 0, 3, SSHS_FLAGS_NORMAL, "TODO."); //4
	atomic_store(&state->chip_id, sshsNodeGetInt(moduleData->moduleNode, "chip_id"));

	sshsNodeAddAttributeListener(moduleData->moduleNode, state, &spikeConfigListener);

	return (true);
}

void caerSoftSpikeGenModuleExit(caerModuleData moduleData) {
	GenSpikeState state = moduleData->moduleState;

	sshsNodeRemoveAttributeListener(moduleData->moduleNode, state, &spikeConfigListener);

	// Shut down stimulation thread and wait on it to finish.
	atomic_store(&state->doStim, false);
	atomic_store(&state->running, false);

	//make sure that doStim is off
	sshsNodePutBool(moduleData->moduleNode, "doStim", false);
	sshsNodePutBool(moduleData->moduleNode, "doStimPrimitiveBias", false);
	sshsNodePutBool(moduleData->moduleNode, "doStimPrimitiveCam", false);

	if ((errno = thrd_join(state->spikeGenThread, NULL)) != thrd_success) {
		// This should never happen!
		caerLog(CAER_LOG_CRITICAL, moduleData->moduleSubSystemString,
			"SpikeGen: Failed to join rendering thread. Error: %d.", errno);
	}

	caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString, "SpikeGenThread: Exited successfully.");
}

void spiketrainETF(GenSpikeState state) {
	if (state == NULL) {
		return;
	}

	struct timespec tim;
	tim.tv_sec = 0;
	float measureMinTime = (float) atomic_load(&state->ETFduration);
	int inFreqs[6] = { 30, 50, 70, 90, 100, 120 };
	int nSteps = 6;
	state->ETFstepnum = nSteps;
	double stepDur = (double) measureMinTime / (double) nSteps;

	int this_step = 0;

	struct timespec ss, dd;
	portable_clock_gettime_monotonic(&ss);

	if (!atomic_load(&state->ETFstarted)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart_etf);
	}

	portable_clock_gettime_monotonic(&tend_etf);

	//check frequency phase and change accordingly
	double current_time = (double) ((double) tend_etf.tv_sec + 1.0e-9 * tend_etf.tv_nsec - (double) tstart_etf.tv_sec
		+ 1.0e-9 * tstart_etf.tv_nsec);
	this_step = 1;
	double chek = round((double) current_time / (double) stepDur);
	if (chek < INT32_MAX && chek > INT32_MIN) {
		this_step = (int32_t) chek;
	}

	atomic_store(&state->ETFphase_num, this_step);
	if (this_step >= 0 && this_step < nSteps) {
		if (inFreqs[this_step] > 0) {
			tim.tv_nsec = 1000000000L / inFreqs[this_step];	// select frequency
		}
		else {
			tim.tv_nsec = 999999999L;
		}
	}
	else {
		tim.tv_nsec = 999999999L; // default value
	}
	if (atomic_load(&state->ETFduration) <= current_time) {
		if (atomic_load(&state->ETFstarted)) {
			//caerLog(CAER_LOG_NOTICE, __func__, "ETF stimulation finished.");
		}
		atomic_store(&state->ETFdone, true);
		atomic_store(&state->ETFstarted, false);
		if (atomic_load(&state->ETFrepeat)) {
			//caerLog(CAER_LOG_NOTICE, __func__, "ETF stimulation re-started.");
			atomic_store(&state->ETFstarted, true);
			atomic_store(&state->ETFdone, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->ETFdone)) {
		uint32_t bits_chipU0[1];

		bits_chipU0[0] = 0xf | 0 << 16 | 0 << 17 | 1 << 13 | 0 << 18 | 5 << 20 | 0 << 4 | 0 << 6 | 0 << 7 | 0 << 9;

		caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
		DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->ETFchip_id));

		// send data with libusb host transfer in packet
		if (!caerDynapseSendDataToUSB(state->sourceDeviceHandle, bits_chipU0, 1)) {
			caerLog(CAER_LOG_ERROR, __func__, "USB transfer failed");
		}

		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* now do the nano sleep */
		nanosleep(&tim, NULL);
	}
}

int spikeGenThread(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return (thrd_error);
	}

	GenSpikeState state = spikeGenState;

	thrd_set_name("SpikeGenThread");

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		if (!atomic_load(&state->doStim) && !atomic_load(&state->setCam) && !atomic_load(&state->setCamSingle) &&
			!atomic_load(&state->clearCam) && !atomic_load(&state->clearAllCam) && !atomic_load(&state->loadDefaultBiases)) {
			struct timespec noStimSleep = { .tv_sec = 0, .tv_nsec = 1000000 };
			thrd_sleep(&noStimSleep, NULL);
			continue;
		}

		if (state->setCam == true && CamSeted == 0) {
			SetCam(state);
			CamSeted = 1;
		}
		else if (state->setCam == false && CamSeted == 1) {
			CamSeted = 0;
		}

		if (state->setCamSingle == true && CamSetedSingle == 0) {
			SetCamSingle(state);
			CamSetedSingle = 1;
		}
		else if (state->setCamSingle == false && CamSetedSingle == 1) {
			CamSetedSingle = 0;
		}

		if (state->clearCam == true && CamCleared == 0) {
			ClearCam(state);
			CamCleared = 1;
		}
		else if (state->clearCam == false && CamCleared == 1) {
			CamCleared = 0;
		}

		if (state->clearAllCam == true && CamAllCleared == 0) {
			ClearAllCam(state);
			CamAllCleared = 1;
		}
		else if (state->clearAllCam == false && CamAllCleared == 1) {
			CamAllCleared = 0;
		}

		if (state->loadDefaultBiases == true && BiasesLoaded == 0) {
			ResetBiases(state);
			BiasesLoaded = 1;
		}
		else if (state->loadDefaultBiases == false && BiasesLoaded == 1) {
			BiasesLoaded = 0;
		}

		/* generate spikes*/
		if (state->stim_type == STIM_REGULAR) {
			spiketrainReg(state);
		}
		else if (state->stim_type == STIM_POISSON) {
			// TODO
		}
		else if (state->stim_type == STIM_GAUSSIAN) {
			// TODO
		}
		else if (state->stim_type == STIM_PATTERNA) {
			// generate pattern A
			uint32_t spikePatternA[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int cx, cy, r;
			cx = 16;
			cy = 16;
			r = 14;
			for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
				for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++)
					spikePatternA[rowId][colId] = 0;
			for (rowId = cx - r; rowId <= cx + r; rowId++)
				for (colId = cy - r; colId <= cy + r; colId++)
					if (((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) <= r * r + sqrt(r))
						&& ((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) >= r * r - r))
						spikePatternA[rowId][colId] = 1;
			spiketrainPat(state, spikePatternA);
		}
		else if (state->stim_type == STIM_PATTERNB) {
			//generate pattern B
			uint32_t spikePatternB[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int64_t num = DYNAPSE_CONFIG_CAMCOL;
			for (rowId = -num; rowId < num; rowId++) {
				for (colId = -num; colId < num; colId++) {
					if (abs((int) rowId) + abs((int) colId) == num) // Change this condition >= <=
						spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
					else
						spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
				}
			}
			spiketrainPat(state, spikePatternB);
		}
		else if (state->stim_type == STIM_PATTERNC) {
			//generate pattern C
			uint32_t spikePatternC[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int64_t num = DYNAPSE_CONFIG_CAMCOL;
			for (rowId = -num; rowId < num; rowId++) {
				for (colId = -num; colId < num; colId++) {
					if (abs((int) rowId) == abs((int) colId)) // Change this condition
						spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
					else
						spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
				}
			}
			spiketrainPat(state, spikePatternC);
		}
		else if (state->stim_type == STIM_PATTERNA_SINGLE) {
			// generate pattern A
			uint32_t sourceAddress = 1;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->stim_type == STIM_PATTERNB_SINGLE) {
			//generate pattern B
			uint32_t sourceAddress = 2;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->stim_type == STIM_PATTERNC_SINGLE) {
			//generate pattern C
			uint32_t sourceAddress = 3;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->stim_type == STIM_PATTERND_SINGLE) {
			//generate pattern D
			uint32_t sourceAddress = 4;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->stim_type == STIM_ETF) {
			spiketrainETF(state);
		}
	}

	return (thrd_success);
}

void spiketrainReg(GenSpikeState state) {
	if (state == NULL) {
		return;
	}

	struct timespec tim;
	tim.tv_sec = 0;

	if (atomic_load(&state->stim_avr) > 0) {
		tim.tv_nsec = 1000000000L / atomic_load(&state->stim_avr);
	}
	else {
		tim.tv_nsec = 999999999L;
	}

	uint32_t value = (uint32_t) atomic_load(&state->core_d) | 0 << 16 | 0 << 17 | 1 << 13
		| (uint32_t) atomic_load(&state->core_s) << 18 | (uint32_t) atomic_load(&state->address) << 20
		| (uint32_t) atomic_load(&state->dx) << 4 | (uint32_t) atomic_load(&state->sx) << 6
		| (uint32_t) atomic_load(&state->dy) << 7 | (uint32_t) atomic_load(&state->sy) << 9;

	if (!atomic_load(&state->started)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart);
	}

	portable_clock_gettime_monotonic(&tend);

	if (atomic_load(&state->stim_duration)
		<= ((double) tend.tv_sec + 1.0e-9 * tend.tv_nsec) - ((double) tstart.tv_sec + 1.0e-9 * tstart.tv_nsec)) {
		if (atomic_load(&state->started)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.");
		}
		atomic_store(&state->done, true);
		atomic_store(&state->started, false);
		if (atomic_load(&state->repeat)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.");
			atomic_store(&state->started, true);
			atomic_store(&state->done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->done)) {
		/* remove time it takes to send, to better match the target freq */
		struct timespec ss, dd;
		portable_clock_gettime_monotonic(&ss);
		/* */
		caerDeviceConfigSet(state->sourceDeviceHandle,
		DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->chip_id)); //usb_handle
		caerDeviceConfigSet(state->sourceDeviceHandle,
		DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, (uint32_t) value); //usb_handle
		/* */
		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* now do the nano sleep */
		nanosleep(&tim, NULL);
	}
}

void spiketrainPat(GenSpikeState state, uint32_t spikePattern[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE]) {
	// generate and send 32*32 input stimuli
	if (state == NULL) {
		return;
	}

	struct timespec tim;
	tim.tv_sec = 0;

	if (atomic_load(&state->stim_avr) > 0) {
		tim.tv_nsec = 1000000000L / atomic_load(&state->stim_avr);
	}
	else {
		tim.tv_nsec = 999999999L;
	}

	//generate chip command for stimulating
	uint32_t value, valueSent;
	uint32_t value2DArray[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	int64_t rowId, colId;
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++) {
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
			if (spikePattern[rowId][colId] == 1)
				value = 0xf | 0 << 16 | 0 << 17 | 1 << 13
					| (uint32_t) (((rowId / DYNAPSE_CONFIG_NEUROW) << 1) | (uint32_t) (colId / DYNAPSE_CONFIG_NEUCOL))
						<< 18
					| (uint32_t) (((rowId % DYNAPSE_CONFIG_NEUROW) << 4) | (uint32_t) (colId % DYNAPSE_CONFIG_NEUCOL))
						<< 20 | (uint32_t) atomic_load(&state->dx) << 4 | (uint32_t) atomic_load(&state->sx) << 6
					| (uint32_t) atomic_load(&state->dy) << 7 | (uint32_t) atomic_load(&state->sy) << 9;
			else {
				value = 0;
			}
			value2DArray[rowId][colId] = value;
		}
	}

	if (!atomic_load(&state->started)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart);
	}

	portable_clock_gettime_monotonic(&tend);

	if (atomic_load(&state->stim_duration)
		<= ((double) tend.tv_sec + 1.0e-9 * (double) tend.tv_nsec)
			- ((double) tstart.tv_sec + 1.0e-9 * (double) tstart.tv_nsec)) {
		if (atomic_load(&state->started)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.");
		}
		atomic_store(&state->done, true);
		atomic_store(&state->started, false);
		if (atomic_load(&state->repeat)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.");
			atomic_store(&state->started, true);
			atomic_store(&state->done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->done)) {

		/* remove time it takes to send, to better match the target freq */
		struct timespec ss, dd;
		portable_clock_gettime_monotonic(&ss);
		/* */

		// send spikes
		caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
		DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->chip_id));
		//send the spike
		for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
			for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
				valueSent = value2DArray[rowId][colId];
				if (valueSent != 0 && ((valueSent >> 18) & 0x3ff) != 0) {
					caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
					DYNAPSE_CONFIG_CHIP_CONTENT, valueSent);
					//caerLog(CAER_LOG_NOTICE, "spikeGen", "sending spikes %d ", valueSent);
				}
			}

		/* */
		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* */
		nanosleep(&tim, NULL);
	}
}

void spiketrainPatSingle(GenSpikeState state, uint32_t sourceAddress) {
	if (state == NULL) {
		return;
	}

	struct timespec tim;
	tim.tv_sec = 0;

	if (atomic_load(&state->stim_avr) > 0) {
		tim.tv_nsec = 1000000000L / atomic_load(&state->stim_avr);
	}
	else {
		tim.tv_nsec = 999999999L;
	}

	// generate chip command for stimulating
	uint32_t valueSent, valueSentTeaching, valueSentTeachingControl, valueSentInhibitory, valueSentInhibitoryControl;
	uint32_t source_address;
	valueSent = 0xf | 0 << 16 | 0 << 17 | 1 << 13 | (uint32_t) (sourceAddress & 0xff) << 20
		| (uint32_t) ((sourceAddress & 0x300) >> 8) << 18 | (uint32_t) atomic_load(&state->dx) << 4
		| (uint32_t) atomic_load(&state->sx) << 6 | (uint32_t) atomic_load(&state->dy) << 7
		| (uint32_t) atomic_load(&state->sy) << 9;

	source_address = 0;
	if (pattern_number == 3) {
		if ((sourceAddress & 0xff) == 1) {
			source_address = 0;
		}
		else if ((sourceAddress & 0xff) == 2) {
			source_address = 4;
		}
		else if ((sourceAddress & 0xff) == 3) {
			source_address = 8;
		}
	}
	else if (pattern_number == 4) {
		if ((sourceAddress & 0xff) == 1) {
			source_address = 0;
		}
		else if ((sourceAddress & 0xff) == 2) {
			source_address = 4;
		}
		else if ((sourceAddress & 0xff) == 3) {
			source_address = 8;
		}
		else if ((sourceAddress & 0xff) == 4) {
			source_address = 12;
		}
	}

	valueSentTeaching = 0x8 | 0 << 16 | 0 << 17 | 1 << 13 | (uint32_t) source_address << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->dx) << 4 | (uint32_t) atomic_load(&state->sx) << 6
		| (uint32_t) atomic_load(&state->dy) << 7 | (uint32_t) atomic_load(&state->sy) << 9; //((sourceAddress & 0x300) >> 8) << 18

	valueSentTeachingControl = 0xc | 0 << 16 | 0 << 17 | 1 << 13 | (uint32_t) source_address << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->dx) << 4 | (uint32_t) atomic_load(&state->sx) << 6 | 1 << 7 | 1 << 9;

	valueSentInhibitory = 0x8 | 0 << 16 | 0 << 17 | 1 << 13 | 3 << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->dx) << 4 | (uint32_t) atomic_load(&state->sx) << 6
		| (uint32_t) atomic_load(&state->dy) << 7 | (uint32_t) atomic_load(&state->sy) << 9; //((sourceAddress & 0x300) >> 8) << 18

	valueSentInhibitoryControl = 0xc | 0 << 16 | 0 << 17 | 1 << 13 | 3 << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->dx) << 4 | (uint32_t) atomic_load(&state->sx) << 6 | 1 << 7 | 1 << 9;

	if (!atomic_load(&state->started)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart);
	}

	portable_clock_gettime_monotonic(&tend);

	if (atomic_load(&state->stim_duration)
		<= ((double) tend.tv_sec + 1.0e-9 * (double) tend.tv_nsec)
			- ((double) tstart.tv_sec + 1.0e-9 * (double) tstart.tv_nsec)) {
		if (atomic_load(&state->started)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.");
		}
		atomic_store(&state->done, true);
		atomic_store(&state->started, false);
		if (atomic_load(&state->repeat)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.");
			atomic_store(&state->started, true);
			atomic_store(&state->done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->done)) {
		/* remove time it takes to send */
		struct timespec ss, dd;
		portable_clock_gettime_monotonic(&ss);
		/* */

		// send spikes
		if (atomic_load(&state->doStimPrimitiveBias) == true && atomic_load(&state->doStimPrimitiveCam) == true) {
			caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->chip_id));
			//send the spike
			caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_CONTENT, valueSent);
			if (atomic_load(&state->teaching) == true && atomic_load(&state->sendTeachingStimuli) == true) {
				caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_ID,
				DYNAPSE_CONFIG_DYNAPSE_U2);
				caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentTeaching);
				caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentTeachingControl);
			}
			if (atomic_load(&state->sendInhibitoryStimuli) == true) { //atomic_load(&state->teaching) == true &&
				caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_ID,
				DYNAPSE_CONFIG_DYNAPSE_U2);
				caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentInhibitory);
				caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentInhibitoryControl);
			}
		}

		/* remove time it took to send, to meet frequency */
		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* now do the nano sleep */
		nanosleep(&tim, NULL);
	}
}

void SetCam(GenSpikeState state) {
	if (state == NULL) {
		return;
	}
	if (atomic_load(&state->running) == false) {
		return;
	}

	caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		(uint32_t) atomic_load(&state->chip_id)); //0

	caerLog(CAER_LOG_NOTICE, __func__, "Started programming cam..");
	for (uint16_t neuronId = 0; neuronId < DYNAPSE_CONFIG_XCHIPSIZE * DYNAPSE_CONFIG_YCHIPSIZE; neuronId++) {
		caerDynapseWriteCam(state->sourceDeviceHandle, neuronId, neuronId, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
	}
	caerLog(CAER_LOG_NOTICE, __func__, "CAM programmed successfully.");
}

void SetCamSingle(GenSpikeState state) {
	if (state == NULL) {
		return;
	}

	caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		(uint32_t) atomic_load(&state->chip_id)); //0

	int64_t rowId, colId;
	int64_t num = DYNAPSE_CONFIG_CAMCOL;
	// generate pattern A
	uint32_t spikePatternA[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	int cx, cy, r;
	cx = 16;
	cy = 16;
	r = 14;
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++)
			spikePatternA[rowId][colId] = 0;
	for (rowId = cx - r; rowId <= cx + r; rowId++)
		for (colId = cy - r; colId <= cy + r; colId++)
			if (((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) <= r * r + sqrt(r))
				&& ((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) >= r * r - r))
				spikePatternA[rowId][colId] = 1;

	uint32_t spikePatternB[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	for (rowId = -num; rowId < num; rowId++) {
		for (colId = -num; colId < num; colId++) {
			if (abs((int) rowId) + abs((int) colId) == num) // Change this condition >= <=
				spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
			else
				spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
		}
	}

	uint32_t spikePatternC[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	for (rowId = -num; rowId < num; rowId++) {
		for (colId = -num; colId < num; colId++) {
			if (abs((int) rowId) == abs((int) colId)) // Change this condition
				spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
			else
				spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
		}
	}

	uint32_t neuronId;
	caerLog(CAER_LOG_NOTICE, __func__, "Started programming cam..");
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++) {
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
			neuronId = (uint32_t) ((rowId & 0X10) >> 4) << 9 | (uint32_t) ((colId & 0X10) >> 4) << 8
				| (uint32_t) (rowId & 0xf) << 4 | (uint32_t) (colId & 0xf);
			if (spikePatternA[rowId][colId] == 1) {
				caerDynapseWriteCam(state->sourceDeviceHandle, 1, neuronId, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
			}
			if (spikePatternB[rowId][colId] == 1) {
				//WriteCam(state, 2, neuronId, 1, 3);
				caerDynapseWriteCam(state->sourceDeviceHandle, 2, neuronId, 1, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
			}
			if (spikePatternC[rowId][colId] == 1) {
				//WriteCam(state, 3, neuronId, 2, 3);
				caerDynapseWriteCam(state->sourceDeviceHandle, 3, neuronId, 2, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
			}
		}
	}

	caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
	DYNAPSE_CONFIG_DYNAPSE_U2); //4, the third chip
	neuronId = 3 << 8 | 0;
	caerDynapseWriteCam(state->sourceDeviceHandle, 1, neuronId, 61, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
	caerDynapseWriteCam(state->sourceDeviceHandle, 2, neuronId, 62, 1);
	caerDynapseWriteCam(state->sourceDeviceHandle, 3, neuronId, 63, 1);
	neuronId = 3 << 8 | 1;
	caerDynapseWriteCam(state->sourceDeviceHandle, 1, neuronId, 61, 1);
	caerDynapseWriteCam(state->sourceDeviceHandle, 2, neuronId, 62, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
	caerDynapseWriteCam(state->sourceDeviceHandle, 3, neuronId, 63, 1);
	neuronId = 3 << 8 | 2;
	caerDynapseWriteCam(state->sourceDeviceHandle, 1, neuronId, 61, 1);
	caerDynapseWriteCam(state->sourceDeviceHandle, 2, neuronId, 62, 1);
	caerDynapseWriteCam(state->sourceDeviceHandle, 3, neuronId, 63, DYNAPSE_CONFIG_CAMTYPE_F_EXC);

	caerLog(CAER_LOG_NOTICE, "SpikeGen", "CAM programmed successfully.");
}

void ClearCam(GenSpikeState state) {
	if (state == NULL) {
		return;
	}

	caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		(uint32_t) atomic_load(&state->chip_id)); //0

	caerLog(CAER_LOG_NOTICE, "SpikeGen", "Started clearing cam...");
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "please wait...");
	for (uint16_t neuronId = 0; neuronId < DYNAPSE_CONFIG_NUMNEURONS; neuronId++) {
		//WriteCam(state, 0, neuronId, 0, 0);
		caerDynapseWriteCam(state->sourceDeviceHandle, 0, neuronId, 0, 0);
	}
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "Done, CAM cleared successfully.");
	atomic_store(&state->clearCam, false);
}

void ClearAllCam(GenSpikeState state) {
	if (state == NULL) {
		return;
	}

	// Select chip-id to operate on.
	caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		U32T(atomic_load(&state->chip_id)));

	// Clear all CAMs on this chip.
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "Started clearing CAM ...");
	caerDeviceConfigSet(state->sourceDeviceHandle, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "CAM cleared successfully.");

	atomic_store(&state->clearAllCam, false);
}

void ResetBiases(GenSpikeState state) {
	if (state == NULL) {
		return;
	}

	caerLog(CAER_LOG_NOTICE, "loadDefaultBiases", "started...");

	uint8_t chipId = U8T(atomic_load(&state->chip_id));

	for (uint8_t coreId = 0; coreId < 4; coreId++) {
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_AHTAU_N", 7, 35, false);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_AHTHR_N", 7, 1, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_AHW_P", 7, 1, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_BUF_P", 3, 80, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_CASC_N", 7, 1, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_DC_P", 5, 2, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_NMDA_N", 7, 1, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_RFR_N", 2, 180, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_TAU1_N", 4, 225, false);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_TAU2_N", 4, 225, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "IF_THR_N", 2, 180, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPIE_TAU_F_P", 6, 150, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPIE_TAU_S_P", 7, 40, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPIE_THR_F_P", 0, 200, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPIE_THR_S_P", 7, 0, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPII_TAU_F_P", 7, 40, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPII_TAU_S_P", 7, 40, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPII_THR_F_P", 7, 40, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "NPDPII_THR_S_P", 7, 40, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 250, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 1, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 1, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 0, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "PULSE_PWLK_P", 3, 50, true);
		caerDynapseSetBiasCore(state->sourceConfigNode, chipId, coreId, "R2R_P", 4, 85, true);
	}
}

static void spikeConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	//caerModuleData moduleData = userData;
	GenSpikeState state = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStim")) { // && caerStrEquals(changeKey, "doStimBias")
			atomic_store(&state->doStim, changeValue.boolean);

			if (changeValue.boolean) {
				//caerModuleLog(CAER_LOG_NOTICE, "spikeGen", "stimulation started.");
				atomic_store(&state->done, false); // we just started
				atomic_store(&state->started, true);
			}
			else {
				//caerModuleLog(CAER_LOG_NOTICE, "spikeGen", "stimulation ended.");
				atomic_store(&state->started, false);
				atomic_store(&state->done, true);
			}
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_type")) {
			atomic_store(&state->stim_type, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_avr")) {
			atomic_store(&state->stim_avr, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_std")) {
			atomic_store(&state->stim_std, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_duration")) {
			atomic_store(&state->stim_duration, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "repeat")) {
			atomic_store(&state->repeat, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "teaching")) {
			atomic_store(&state->teaching, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sendTeachingStimuli")) {
			atomic_store(&state->sendTeachingStimuli, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sendInhibitoryStimuli")) {
			atomic_store(&state->sendInhibitoryStimuli, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "setCam")) {
			atomic_store(&state->setCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "setCamSingle")) {
			atomic_store(&state->setCamSingle, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "clearCam")) {
			atomic_store(&state->clearCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "clearAllCam")) {
			atomic_store(&state->clearAllCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStimPrimitiveBias")) {
			atomic_store(&state->doStimPrimitiveBias, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStimPrimitiveCam")) {
			atomic_store(&state->doStimPrimitiveCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "loadDefaultBiases")) {
			atomic_store(&state->loadDefaultBiases, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
			atomic_store(&state->running, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sx")) {
			atomic_store(&state->sx, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sy")) {
			atomic_store(&state->sy, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "dx")) {
			atomic_store(&state->dx, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "dy")) {
			atomic_store(&state->dy, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "core_d")) {
			atomic_store(&state->core_d, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "core_s")) {
			atomic_store(&state->core_s, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "address")) {
			atomic_store(&state->address, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "chip_id")) {
			atomic_store(&state->chip_id, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFphase_num")) {
			atomic_store(&state->ETFphase_num, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFstarted")) {
			atomic_store(&state->ETFstarted, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFdone")) {
			atomic_store(&state->ETFdone, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFchip_id")) {
			atomic_store(&state->ETFchip_id, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFduration")) {
			atomic_store(&state->ETFduration, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFphase_num")) {
			atomic_store(&state->ETFphase_num, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFrepeat")) {
			atomic_store(&state->ETFrepeat, changeValue.boolean);
		}
	}
}
