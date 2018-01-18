/*
 * arduino control via serial port
 *
 *  Created on: Nov 2016, ported on Jan 2018 to new caer module
 *      Author: federico.corradi@inilabs.com
 */

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include "arduino-serial-lib.h"
#include <libcaer/events/point1d.h>

//majory voting thread
#ifdef HAVE_PTHREADS
#include "ext/c11threads_posix.h"
#endif
#include "stdatomic.h"

#define ROCK 3
#define PAPER 1
#define SCISSORS 2
#define BACKGROUND 4	//network output unit number one based (starting from one)
#define AVERAGEOVER 1

struct ASFilter_state {
	int fd;
	int baudRate;
	int timeout;
	char * serialPort;
	thrd_t majorityThread;
	atomic_bool running;
	uint16_t pos;
	uint16_t lastcommand;
	atomic_int_fast32_t decision[AVERAGEOVER];
};

typedef struct ASFilter_state *ASFilterState;

static bool caerArduinoCNTInit(caerModuleData moduleData);
static void caerArduinoCNTRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out); 
static void caerArduinoCNTConfig(caerModuleData moduleData);
static void caerArduinoCNTExit(caerModuleData moduleData);
static void caerArduinoCNTReset(caerModuleData moduleData,
		uint16_t resetCallSourceID);

int majorityThread(void *ASFilter_state);

static const struct caer_module_functions caerArduinoCNTFunctions = { .moduleInit =
		&caerArduinoCNTInit, .moduleRun = &caerArduinoCNTRun, .moduleConfig =
		&caerArduinoCNTConfig, .moduleExit = &caerArduinoCNTExit, .moduleReset =
		&caerArduinoCNTReset };

static const struct caer_event_stream_in caerArduinoCNTInputs[] = 
		{{ .type = POINT1D_EVENT, .number = 1, .readOnly = true }};

static const struct caer_module_info caerArduinoCNTInfo = { .version = 1 , .name = "caerArduinoCNT" , .description = "Control Arudino via CH341 driver", .type = CAER_MODULE_OUTPUT , .memSize=sizeof(struct ASFilter_state), .functions = &caerArduinoCNTFunctions, .inputStreams = caerArduinoCNTInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(caerArduinoCNTInputs), .outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void){
      return (&caerArduinoCNTInfo);
}

int majorityThread(void *ASFilter_state) {

	if (ASFilter_state == NULL) {
		return (thrd_error);
	}

	ASFilterState state = ASFilter_state;

	thrd_set_name("ArduinoCNTThread");

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		/*do majority voting*/

		for (size_t i = 0; i < AVERAGEOVER; i++) {
			atomic_load(&state->decision[i]);
		}
		int paper = 0;
		int rock = 0;
		int scissors = 0;
		int back =0;
		for (size_t i = 0; i < AVERAGEOVER; i++) {
			if (state->decision[i] == PAPER) {
				paper++;
			} else if (state->decision[i] == SCISSORS) {
				scissors++;
			} else if (state->decision[i] == ROCK) {
				rock++;
			} else if (state->decision[i] == BACKGROUND) {
				back++;
			}
		}

		char res[20];
		int16_t current_dec;
		if ((rock > paper) && (rock > scissors) && (rock > back)) {
			/*play rock*/
			sprintf(res, "%d", ROCK);
			current_dec = ROCK;
		} else if ((back > paper) && (back > scissors) && (back > rock)) {
			/*play back*/
			sprintf(res, "%d", BACKGROUND);
			current_dec = BACKGROUND;
		} else if ((scissors > paper) && (scissors > rock)
				&& (scissors > back)) {
			/*play scissors*/
			sprintf(res, "%d", SCISSORS);
			current_dec = SCISSORS;
		} else if ((paper > scissors) && (paper > rock) && (paper > back)) {
			/*play paper*/
			sprintf(res, "%d", PAPER);
			current_dec = PAPER;
		}else{
			current_dec = state->lastcommand;
		}
		if(current_dec != state->lastcommand){

			caerLog(CAER_LOG_DEBUG, "ArduinoCNT", "####################### sending to arduino %d\n\n", current_dec);
			serialport_write(state->fd, res);
			state->lastcommand = current_dec;
		}
	}

	return (thrd_success);
}

static bool caerArduinoCNTInit(caerModuleData moduleData) {

	sshsNodeCreateString(moduleData->moduleNode, "serialPort",
			"/dev/ttyUSB0", 0, 2048, SSHS_FLAGS_NORMAL, "serial port address");
	sshsNodeCreateInt(moduleData->moduleNode, "baudRate", 115200, 0, 115200, SSHS_FLAGS_NORMAL, "Baudrate of com port");
	sshsNodeCreateInt(moduleData->moduleNode, "timeout", 5000, 0, 5000, SSHS_FLAGS_NORMAL , "timeout for sending command");

	ASFilterState state = moduleData->moduleState;

	state->serialPort = (char*) calloc(1024, sizeof(char));
	state->serialPort = sshsNodeGetString(moduleData->moduleNode, "serialPort");
	state->baudRate = sshsNodeGetInt(moduleData->moduleNode, "baudRate");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData,
			&caerModuleConfigDefaultListener);

	//open Serial PORT
	state->fd = -1;
	state->fd = serialport_init(state->serialPort, state->baudRate);
	if (state->fd == -1) {
		caerLog(CAER_LOG_CRITICAL, "arduinoCNT", "failed to open usb port");
		exit(1);
	}
	serialport_flush(state->fd);

	atomic_store(&state->pos, 0);
	for (size_t i = 0; i < AVERAGEOVER; i++) {
		atomic_store(&state->decision[i], BACKGROUND);
	}
	state->lastcommand = BACKGROUND;
	state->pos = 0;

	//start thread for arm control
	if (thrd_create(&state->majorityThread, &majorityThread, state)
			!= thrd_success) {
		//ringBufferFree(state->dataTransfer);
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
				"Majority voting thread failed to initialize");
		exit (false);
	}else{
		atomic_store(&state->running,true);
	}


	// Nothing that can fail here.
	return (true);
}

static void caerArduinoCNTRun(caerModuleData moduleData, caerEventPacketContainer in, 
	caerEventPacketContainer *out) {

	// Interpret variable arguments (same as above in main function).
	caerPoint1DEventPacket result = (caerPoint1DEventPacket) caerEventPacketContainerFindEventPacketByTypeConst(in, POINT1D_EVENT);
	ASFilterState state = moduleData->moduleState;


	CAER_POINT1D_ITERATOR_ALL_START(result)
		int this_res = caerPoint1DEventGetX(caerPoint1DIteratorElement);
		atomic_store(&state->decision[state->pos], this_res);
		if (state->pos == AVERAGEOVER) {
			state->pos = 0;
		} else {
			state->pos = state->pos + 1;
		}
	CAER_POINT1D_ITERATOR_ALL_END

	//
}

static void caerArduinoCNTConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	ASFilterState state = moduleData->moduleState;

	state->serialPort = sshsNodeGetString(moduleData->moduleNode, "serialPort");
	state->baudRate = sshsNodeGetInt(moduleData->moduleNode, "baudRate");

}

static void caerArduinoCNTExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData,
			&caerModuleConfigDefaultListener);

	ASFilterState state = moduleData->moduleState;

	free(state->serialPort);

	//close tread
	atomic_store(&state->running, false);

	if ((errno = thrd_join(state->majorityThread, NULL)) != thrd_success) {
		caerLog(CAER_LOG_CRITICAL, moduleData->moduleSubSystemString,
				"failed to join majority voting thread error: %d\n", errno);
	}
	serialport_write(state->fd, "5\n");
	serialport_close(state->fd);
}

static void caerArduinoCNTReset(caerModuleData moduleData,
		uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	ASFilterState state = moduleData->moduleState;
}

