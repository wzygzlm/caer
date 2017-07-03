/*
 * poissonspikegen.c
 *
 *  Created on: May 2017 for the poisson generator on dynap-se
 *      Author: Carsten
 */

#include "poissonspikegen.h"
#include "base/mainloop.h"
#include "base/module.h"

struct HWFilter_state {
	// user settings
	uint32_t neuronAddr;
	double rateHz;
	bool run;
	bool update;
	bool loadRatesFromFile;
	char* rateFile;
	int chipID;
	bool programTestPattern;
	// usb utils
	caerInputDynapseState eventSourceModuleState;
	sshsNode eventSourceConfigNode;
};

typedef struct HWFilter_state *HWFilterState;

static bool caerPoissonSpikeGenModuleInit(caerModuleData moduleData);
static void caerPoissonSpikeGenModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerPoissonSpikeGenModuleConfig(caerModuleData moduleData);
static void caerPoissonSpikeGenModuleExit(caerModuleData moduleData);
static void caerPoissonSpikeGenModuleReset(caerModuleData moduleData, uint16_t resetCallSourceID);
void loadRatesFromFile(caerModuleData moduleData, char* fileName);
void loadProgramTestPattern(caerModuleData moduleData);

static struct caer_module_functions caerPoissonSpikeGenModuleFunctions = { .moduleInit =
	&caerPoissonSpikeGenModuleInit, .moduleRun = &caerPoissonSpikeGenModuleRun, .moduleConfig =
	&caerPoissonSpikeGenModuleConfig, .moduleExit = &caerPoissonSpikeGenModuleExit, .moduleReset =
	&caerPoissonSpikeGenModuleReset };

void caerPoissonSpikeGenModule(uint16_t moduleID, caerSpikeEventPacket spike) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "Poisson-SpikeGen", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}

	caerModuleSM(&caerPoissonSpikeGenModuleFunctions, moduleData, sizeof(struct HWFilter_state), 1, spike);
}

static bool caerPoissonSpikeGenModuleInit(caerModuleData moduleData) {
	// create parameters
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "loadBiases", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setSram", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setCam", false);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Run", false);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Update", false);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Load rates from file", false);
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "Rate file", "");
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "Target neuron address", 0);
	sshsNodePutDoubleIfAbsent(moduleData->moduleNode, "Rate (Hz)", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "Chip ID", 0);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "Program test pattern", false);

	HWFilterState state = moduleData->moduleState;

	// update node state
	state->neuronAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Target neuron address");
	state->rateHz = sshsNodeGetDouble(moduleData->moduleNode, "Rate (Hz)");
	state->run = sshsNodeGetBool(moduleData->moduleNode, "Run");
	state->update = sshsNodeGetBool(moduleData->moduleNode, "Update");
	state->loadRatesFromFile = sshsNodeGetBool(moduleData->moduleNode, "Load rates from file");
	state->rateFile = sshsNodeGetString(moduleData->moduleNode, "Rate file");
	state->chipID = sshsNodeGetInt(moduleData->moduleNode, "Chip ID");
	state->programTestPattern = sshsNodeGetBool(moduleData->moduleNode, "Program test pattern");

	if (caerStrEquals(state->rateFile, "")) {
		free(state->rateFile);
	}


	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerPoissonSpikeGenModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
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

static void caerPoissonSpikeGenModuleConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	HWFilterState state = moduleData->moduleState;
	caerInputDynapseState stateSource = state->eventSourceModuleState;

	// this will update parameters, from user input
	bool newUpdate = sshsNodeGetBool(moduleData->moduleNode, "Update");
	bool newRun = sshsNodeGetBool(moduleData->moduleNode, "Run");
	bool newProgramTestPattern = sshsNodeGetBool(moduleData->moduleNode, "Program test pattern");


	// These parameters are always safe to update
	state->loadRatesFromFile = sshsNodeGetBool(moduleData->moduleNode, "Load rates from file");

	// Change run state if necessary
	if (newRun && !state->run) {
		state->run = true;
		caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_RUN,
				    1);
		state->chipID = sshsNodeGetInt(moduleData->moduleNode, "Chip ID");
		switch (state->chipID) {
		case 0:
		    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U0 );
		    break;
		case 1:
		    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U1 );
		    break;
		case 2:
		    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U2 );
		    break;
		case 3:
		    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U3 );
		    break;
		}
	}
	else if (!newRun && state->run) {
		state->run = false;
		caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_RUN,
				    0);
	}

	// Update the poisson rates from file or gui if necessary
	if (newUpdate && !state->update) {
		state->update = true;
		if (state->loadRatesFromFile) {
			// parse the file and update the poisson source rates
			state->rateFile = sshsNodeGetString(moduleData->moduleNode, "Rate file");
			loadRatesFromFile(moduleData, state->rateFile);
		}
		else {
			// otherwise, just update the single address/rate pair from the gui
			state->neuronAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Target neuron address");
			state->rateHz = sshsNodeGetDouble(moduleData->moduleNode, "Rate (Hz)");
			caerDynapseWritePoissonSpikeRate(stateSource->deviceState, state->neuronAddr, state->rateHz);
		}
	}
	else if (!newUpdate && state->update) {
		state->update = false;
	}

	if (newProgramTestPattern && !state->programTestPattern) {
		state->programTestPattern = true;
		loadProgramTestPattern(moduleData);
	}
	else if (!newProgramTestPattern && state->programTestPattern) {
		state->programTestPattern = false;
	}
}

// program a hard coded test pattern for easy visual verification of the poisson spike generator
void loadProgramTestPattern(caerModuleData moduleData) {
	HWFilterState state = moduleData->moduleState;
	caerInputDynapseState stateSource = state->eventSourceModuleState;

	state->chipID = sshsNodeGetInt(moduleData->moduleNode, "Chip ID");
	switch (state->chipID) {
	case 0:
	    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U0 );
	    break;
	case 1:
	    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U1 );
	    break;
	case 2:
	    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U2 );
	    break;
	case 3:
	    caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, DYNAPSE_CONFIG_DYNAPSE_U3 );
	    break;
	}

	for (uint32_t i = 0; i < DYNAPSE_CONFIG_NUMNEURONS; i++) {
		caerDynapseWriteCam(stateSource->deviceState, 0, i, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC );
	}
	caerDynapseWritePoissonSpikeRate(stateSource->deviceState, (uint32_t)0, (double)10.0);
}


void loadRatesFromFile(caerModuleData moduleData, char* fileName) {
	HWFilterState state = moduleData->moduleState;
	caerInputDynapseState stateSource = state->eventSourceModuleState;

	FILE *fp = fopen(fileName, "r");

	if (fp == NULL) {
		caerLog(CAER_LOG_ERROR, __func__, "Could not open poisson rate file\n");
		return;
	}

	// get number of lines accounting for possible empty line at the end
	int c;
	int last = '\n';
	uint32_t lines = 0;

	while ((c = getc(fp)) != EOF) {
		if (c == '\n') {
			lines += 1;
		}
		last = c;
	}
	if (last != '\n') {
		lines += 1;
	}

	// instantiate array that will hold the 1024 spike rates, many may be 0, but we need to set that
	// anyway
	double rateArray[1024] = {0};

	// read the file again to extract the addresses and rates.
	// format is: "address, rate\n"
	rewind(fp);
	uint16_t address;
	double rate;
	for (uint32_t i = 0; i < lines; i++) {
		fscanf(fp, "%hu, %lf\n", &address, &rate);
		if (address > 1023) {
			caerLog(CAER_LOG_ERROR, __func__, "Poisson address out of bounds");
			return;
		}
		rateArray[address] = rate;
	}

	// Now we can write the rates to the poisson generator on the dynap-se
	for (uint32_t i = 0; i < 1024; i++) {
		caerDynapseWritePoissonSpikeRate(stateSource->deviceState, i, rateArray[i]);
	}
}

static void caerPoissonSpikeGenModuleExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	HWFilterState state = moduleData->moduleState;

	// here we should free memory and other shutdown procedures if needed

}

static void caerPoissonSpikeGenModuleReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	HWFilterState state = moduleData->moduleState;

}
