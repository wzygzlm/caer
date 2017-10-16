/*
 * fpgaspikegen.c
 *
 */

#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/devices/dynapse.h>

#include <libcaer/events/spike.h>


struct HWFilter_state {
	// user settings
	uint32_t baseAddr;
	uint32_t isiBase;
	uint32_t isi;
	bool varMode;
	uint32_t stimCount;
	bool run;
	char *stimFile;
	bool writeSram;
	bool repeat;
	// usb utils
	caerDeviceHandle eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	int16_t sourceID;
	int16_t ChipID;
};

typedef struct HWFilter_state *HWFilterState;

static bool caerFpgaSpikeGenModuleInit(caerModuleData moduleData);
static void caerFpgaSpikeGenModuleRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerFpgaSpikeGenModuleConfig(caerModuleData moduleData);
static void caerFpgaSpikeGenModuleExit(caerModuleData moduleData);
static void caerFpgaSpikeGenModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);
void fixedIsiFileToSram(caerModuleData moduleData, char* fileName);
void variableIsiFileToSram(caerModuleData moduleData, char* fileName);

static struct caer_module_functions caerFpgaSpikeGenModuleFunctions = { .moduleInit =
	&caerFpgaSpikeGenModuleInit, .moduleRun = &caerFpgaSpikeGenModuleRun, .moduleConfig =
	&caerFpgaSpikeGenModuleConfig, .moduleExit = &caerFpgaSpikeGenModuleExit, .moduleReset =
	&caerFpgaSpikeGenModuleReset };

static const struct caer_event_stream_in moduleInputs[] = {
    { .type = SPIKE_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "SpikeGen",
	.description = "SpikeGenerator via FPGA",
	.type = CAER_MODULE_OUTPUT,
	.memSize = sizeof(struct HWFilter_state),
	.functions = &caerFpgaSpikeGenModuleFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL,
	.outputStreamsSize = 0
};

// init

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}

static bool caerFpgaSpikeGenModuleInit(caerModuleData moduleData) {

	HWFilterState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	state->sourceID = inputs[0];
	free(inputs);

	// create parameters
	sshsNodeCreateInt(moduleData->moduleNode, "ChipID", 0, 0, 3, SSHS_FLAGS_NORMAL, "Target Chip Id, where the spikes will be directed to, not yet implemented (chipID is always = U0)");
	sshsNodeCreateInt(moduleData->moduleNode, "ISI", 10, 0, 1000, SSHS_FLAGS_NORMAL, "Inter Spike Interval, in terms of ISIbase (ISIBase*ISI), only used if Variable ISI is not selected");
	sshsNodeCreateInt(moduleData->moduleNode, "ISIBase", 1, 0, 1000, SSHS_FLAGS_NORMAL, "Inter Spike Interval multiplier in us");
	sshsNodeCreateBool(moduleData->moduleNode, "Run", false, SSHS_FLAGS_NORMAL, "Start/Stop Stimulation. It will finish a complete stimulation before ending.");
	sshsNodeCreateInt(moduleData->moduleNode, "BaseAddress", 0, 0, 1024, SSHS_FLAGS_NORMAL, "");
	sshsNodeCreateBool(moduleData->moduleNode, "VariableISI", false, SSHS_FLAGS_NORMAL, "Use variable interspike intervals");
	sshsNodeCreateBool(moduleData->moduleNode, "WriteSRAM", false, SSHS_FLAGS_NORMAL, "Write Sram content from file");
	sshsNodeCreateString(moduleData->moduleNode, "StimFile", "default.txt", 1, 2048, SSHS_FLAGS_NORMAL, "File containing the stimuli, see manual for file format, example in modules/fpgaspikegenerator/data/generate_input.py");
	sshsNodeCreateBool(moduleData->moduleNode, "Repeat", false, SSHS_FLAGS_NORMAL, "Repeat stimulation once finished");

	// update node state
	state->ChipID = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ChipID");
	state->isi = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISI");
	state->run = sshsNodeGetBool(moduleData->moduleNode, "Run");
	state->isiBase = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISIBase");
	state->varMode = sshsNodeGetBool(moduleData->moduleNode, "VariableISI");
	state->baseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "BaseAddress");
	state->stimFile = sshsNodeGetString(moduleData->moduleNode, "StimFile");
	state->stimCount = 0;
	state->writeSram = sshsNodeGetBool(moduleData->moduleNode, "WriteSRAM");
	state->repeat = sshsNodeGetBool(moduleData->moduleNode, "Repeat");

	if (caerStrEquals(state->stimFile, "")) {
		free(state->stimFile);
	}

	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	caerLog(CAER_LOG_NOTICE, __func__, "Inizialized fpga spikegen");

	state->eventSourceModuleState = caerMainloopGetSourceState(U16T(state->sourceID));
	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(state->sourceID));

	return (true);
}

static void caerFpgaSpikeGenModuleRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerSpikeEventPacketConst spike =
		(caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, SPIKE_EVENT);

	// Only process packets with content.
	if (spike == NULL) {
		return;
	}

}

static void caerFpgaSpikeGenModuleConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	HWFilterState state = moduleData->moduleState;

	// this will update parameters, from user input
	bool newRun = sshsNodeGetBool(moduleData->moduleNode, "Run");
	bool newWriteSram = sshsNodeGetBool(moduleData->moduleNode, "WriteSRAM");

	if (newWriteSram && !state->writeSram) {
		// To update the SRAM we need to grab the file containing our spike train, whether
		// we are in variable ISI mode or not and what the base address of the train in memory is
		state->writeSram = true;
		state->stimFile = sshsNodeGetString(moduleData->moduleNode, "StimFile");
		state->varMode = sshsNodeGetBool(moduleData->moduleNode, "VariableISI");
		state->baseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "BaseAddress");
		
		if (state->varMode) {
			variableIsiFileToSram(moduleData, state->stimFile);
		}
		else {
			fixedIsiFileToSram(moduleData, state->stimFile);
		}

	}
	else if (!newWriteSram && state->writeSram) {
		state->writeSram = false;
	}

	if (newRun && !state->run) {
		state->run = true;
		state->ChipID = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ChipID");
		state->isi = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISI");
		state->isiBase = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISIBase");
		state->varMode = sshsNodeGetBool(moduleData->moduleNode, "VariableISI");
		state->baseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "BaseAddress");
		state->repeat = sshsNodeGetBool(moduleData->moduleNode, "Repeat");

		int retval;
		retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN, state->isi);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "ISI failed to update");
		}
		retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_ISI, state->isi);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "ISI failed to update");
		}
		retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_ISIBASE, state->isiBase);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "ISI base failed to update");
		}
		retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_VARMODE, state->varMode);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "varMode failed to update");
		}
		retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_BASEADDR, state->baseAddr);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "Base address failed to update");
		}
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_REPEAT, state->repeat);

		retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_RUN, state->run);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "run status failed to update");
		}

	}
	else if (!newRun && state->run) {
		state->run = false;
	}

}

void fixedIsiFileToSram(caerModuleData moduleData, char* fileName) {
	HWFilterState state = moduleData->moduleState;

	FILE *fp = fopen(fileName, "r");

	if (fp == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Could not open fixed ISI file\n");
		return;
	}

	// get the number of lines
	int c;
	int last = '\n';
	uint32_t lines = 0;

	while ((c = getc(fp)) != EOF) {  /* Count word line endings. */
		if (c == '\n') {
			lines += 1;
		}
		last = c;
	}
	if (last != '\n') {
		lines += 1;
	}

	// array holding values to be written to sram
	uint16_t *spikeTrain = malloc(lines * sizeof(*spikeTrain));

	if (spikeTrain == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Could not allocate memory for spike train vector\n");
		return;
	}

	// read the file again to get the events
	rewind(fp);
	for (uint32_t i = 0; i < lines; i++) {
		fscanf(fp, "%hu\n", (uint16_t*)(spikeTrain + i));
	}
	fclose(fp);

	/* update stim count with lenght of lines */
	state->stimCount = lines - 1;
	int retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_STIMCOUNT, state->stimCount);
	if ( retval == 0 ) {
		caerLog(CAER_LOG_NOTICE, __func__, "stimcount failed to update");
	}

	caerLog(CAER_LOG_NOTICE, __func__, "Wrote spike train of length %u to memory with base address %u\n", lines, state->baseAddr);

	// write them to the dynapse
	caerDynapseWriteSramWords(state->eventSourceModuleState, spikeTrain, state->baseAddr, lines);

	free(spikeTrain);
	
	return;
}

void variableIsiFileToSram(caerModuleData moduleData, char* fileName) {
	HWFilterState state = moduleData->moduleState;

	FILE *fp = fopen(fileName, "r");

	if (fp == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Could not open variable ISI file\n");
		return;
	}

	// get the number of lines
	int c;
	int last = '\n';
	uint32_t lines = 0;

	while ((c = getc(fp)) != EOF) {  /* Count word line endings. */
		if (c == '\n') {
			lines += 1;
		}
		last = c;
	}
	if (last != '\n') {
		lines += 1;
	}

	// array holding values to be written to sram, events and interspike intervals
	uint16_t *spikeTrain = malloc(2 * lines * sizeof(*spikeTrain));

	if (spikeTrain == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Could not allocate memory for spike train vector\n");
		return;
	}

	// read the file again to get the events
	rewind(fp);
	for (uint32_t i = 0; i < 2*lines; i+=2) {
		fscanf(fp, "%hu, %hu\n", (uint16_t*)(spikeTrain + i), (uint16_t*)(spikeTrain + i + 1));
	}
	fclose(fp);

	/* update stim count with lenght from file */
	state->stimCount = lines - 1;
	int retval = caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_STIMCOUNT, state->stimCount);
	if ( retval == 0 ) {
		caerLog(CAER_LOG_NOTICE, __func__, "stimcount failed to update");
	}

	caerLog(CAER_LOG_NOTICE, __func__, "Wrote spike train of length %u to memory with base address %u\n", lines, state->baseAddr);

	// write them to the dynapse
	caerDynapseWriteSramWords(state->eventSourceModuleState, spikeTrain, state->baseAddr, 2*lines);

	free(spikeTrain);

	return;
}

static void caerFpgaSpikeGenModuleExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	HWFilterState state = moduleData->moduleState;

}

static void caerFpgaSpikeGenModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	HWFilterState state = moduleData->moduleState;

}
