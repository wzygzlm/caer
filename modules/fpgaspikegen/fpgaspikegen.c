/*
 * fpgaspikegen.c
 *
 */

#include "fpgaspikegen.h"
#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/devices/dynapse.h>
#include "modules/ini/dynapse_common.h"

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
	caerInputDynapseState eventSourceModuleState;
	sshsNode eventSourceConfigNode;
};

typedef struct HWFilter_state *HWFilterState;

static bool caerFpgaSpikeGenModuleInit(caerModuleData moduleData);
static void caerFpgaSpikeGenModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerFpgaSpikeGenModuleConfig(caerModuleData moduleData);
static void caerFpgaSpikeGenModuleExit(caerModuleData moduleData);
static void caerFpgaSpikeGenModuleReset(caerModuleData moduleData, uint16_t resetCallSourceID);
void fixedIsiFileToSram(caerModuleData moduleData, char* fileName);
void variableIsiFileToSram(caerModuleData moduleData, char* fileName);

static struct caer_module_functions caerFpgaSpikeGenModuleFunctions = { .moduleInit =
	&caerFpgaSpikeGenModuleInit, .moduleRun = &caerFpgaSpikeGenModuleRun, .moduleConfig =
	&caerFpgaSpikeGenModuleConfig, .moduleExit = &caerFpgaSpikeGenModuleExit, .moduleReset =
	&caerFpgaSpikeGenModuleReset };

void caerFpgaSpikeGenModule(uint16_t moduleID, caerSpikeEventPacket spike) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "FPGA-SpikeGen", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}

	caerModuleSM(&caerFpgaSpikeGenModuleFunctions, moduleData, sizeof(struct HWFilter_state), 1, spike);
}

static bool caerFpgaSpikeGenModuleInit(caerModuleData moduleData) {
	caerLog(CAER_LOG_NOTICE, __func__, "start init of fpga spikegen");
	// create parameters
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "ISI", 10);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "ISI base", 1);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Run", false);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "Stim count", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "Base address", 0);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Variable ISI", false);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Write SRAM", false);
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "Stim file", "");
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Repeat", false);

	HWFilterState state = moduleData->moduleState;

	// update node state
	state->isi = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISI");
	state->run = sshsNodeGetBool(moduleData->moduleNode, "Run");
	state->isiBase = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISI base");
	state->varMode = sshsNodeGetBool(moduleData->moduleNode, "Variable ISI");
	state->baseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Base address");
	state->stimFile = sshsNodeGetString(moduleData->moduleNode, "Stim file");
	state->stimCount = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Stim count");
	state->writeSram = sshsNodeGetBool(moduleData->moduleNode, "Write SRAM");
	state->repeat = sshsNodeGetBool(moduleData->moduleNode, "Repeat");

	if (caerStrEquals(state->stimFile, "")) {
		free(state->stimFile);
	}

	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerFpgaSpikeGenModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).
	caerSpikeEventPacket spike = va_arg(args, caerSpikeEventPacket);

	// Only process packets with content.
	if (spike == NULL) {
		return;
	}

	HWFilterState state = moduleData->moduleState;

  	// now we can do crazy processing etc..
	// first find out which one is the module producing the spikes. and get usb handle
	// --- start  usb handle / from spike event source id
	int sourceID = caerEventPacketHeaderGetEventSource(&spike->packetHeader);
	state->eventSourceModuleState = caerMainloopGetSourceState(U16T(sourceID));
	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(sourceID));
	if(state->eventSourceModuleState == NULL || state->eventSourceConfigNode == NULL){
		return;
	}
	caerInputDynapseState stateSource = state->eventSourceModuleState;
	if(stateSource->deviceState == NULL){
		return;
	}
	// --- end usb handle



}

static void caerFpgaSpikeGenModuleConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	HWFilterState state = moduleData->moduleState;
	caerInputDynapseState stateSource = state->eventSourceModuleState;

	// this will update parameters, from user input
	bool newRun = sshsNodeGetBool(moduleData->moduleNode, "Run");
	bool newWriteSram = sshsNodeGetBool(moduleData->moduleNode, "Write SRAM");

	if (newWriteSram && !state->writeSram) {
		// To update the SRAM we need to grab the file containing our spike train, whether
		// we are in variable ISI mode or not and what the base address of the train in memory is
		state->writeSram = true;
		state->stimFile = sshsNodeGetString(moduleData->moduleNode, "Stim file");
		state->varMode = sshsNodeGetBool(moduleData->moduleNode, "Variable ISI");
		state->baseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Base address");
		
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
		state->isi = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISI");
		state->isiBase = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "ISI base");
		state->varMode = sshsNodeGetBool(moduleData->moduleNode, "Variable ISI");
		state->baseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Base address");
		state->stimCount = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Stim count");
		state->repeat = sshsNodeGetBool(moduleData->moduleNode, "Repeat");

		int retval;
		retval = caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_ISI, state->isi);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "ISI failed to update");
		}
		retval = caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_ISIBASE, state->isiBase);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "ISI base failed to update");
		}
		retval = caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_VARMODE, state->varMode);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "varMode failed to update");
		}
		retval = caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_BASEADDR, state->baseAddr);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "Base address failed to update");
		}
		retval = caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_STIMCOUNT, state->stimCount - 1);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "stimcount failed to update");
		}
		retval = caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_RUN, state->run);
		if ( retval == 0 ) {
			caerLog(CAER_LOG_NOTICE, __func__, "run status failed to update");
		}

		caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_SPIKEGEN, DYNAPSE_CONFIG_SPIKEGEN_REPEAT, state->repeat);
	}
	else if (!newRun && state->run) {
		state->run = false;
	}

}

void fixedIsiFileToSram(caerModuleData moduleData, char* fileName) {
	HWFilterState state = moduleData->moduleState;
	caerInputDynapseState stateSource = state->eventSourceModuleState;

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

	caerLog(CAER_LOG_NOTICE, __func__, "Wrote spike train of length %u to memory with base address %u\n", lines, state->baseAddr);

	// write them to the dynapse
	caerDynapseWriteSramWords(stateSource->deviceState, spikeTrain, state->baseAddr, lines);

	free(spikeTrain);
	
	return;
}

void variableIsiFileToSram(caerModuleData moduleData, char* fileName) {
	HWFilterState state = moduleData->moduleState;
	caerInputDynapseState stateSource = state->eventSourceModuleState;

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

	caerLog(CAER_LOG_NOTICE, __func__, "Wrote spike train of length %u to memory with base address %u\n", lines, state->baseAddr);

	// write them to the dynapse
	caerDynapseWriteSramWords(stateSource->deviceState, spikeTrain, state->baseAddr, 2*lines);

	free(spikeTrain);

	return;
}

static void caerFpgaSpikeGenModuleExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	HWFilterState state = moduleData->moduleState;

	// here we should free memory and other shutdown procedures if needed

}

static void caerFpgaSpikeGenModuleReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	HWFilterState state = moduleData->moduleState;

}
