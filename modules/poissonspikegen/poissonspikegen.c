/*
 * poissonspikegen.c
 *
 *  Created on: May 2017 for the poisson generator on dynap-se
 *      Author: Carsten
 */

#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/events/spike.h>
#include <libcaer/devices/dynapse.h>

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
	caerDeviceHandle eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	int16_t sourceID;
};

typedef struct HWFilter_state *HWFilterState;

static bool caerPoissonSpikeGenModuleInit(caerModuleData moduleData);
static void caerPoissonSpikeGenModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
    caerEventPacketContainer *out);
static void caerPoissonSpikeGenModuleConfig(caerModuleData moduleData);
static void caerPoissonSpikeGenModuleExit(caerModuleData moduleData);
static void caerPoissonSpikeGenModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);
void loadRatesFromFile(caerModuleData moduleData, char* fileName);
void loadProgramTestPattern(caerModuleData moduleData);

static struct caer_module_functions caerPoissonSpikeGenModuleFunctions = { .moduleInit =
	&caerPoissonSpikeGenModuleInit, .moduleRun = &caerPoissonSpikeGenModuleRun, .moduleConfig =
	&caerPoissonSpikeGenModuleConfig, .moduleExit = &caerPoissonSpikeGenModuleExit, .moduleReset =
	&caerPoissonSpikeGenModuleReset };

static const struct caer_event_stream_in moduleInputs[] = {
    { .type = SPIKE_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_module_info moduleInfo = {
    .version = 1,
    .name = "Poisson-SpikeGen",
	.description = "Poisson FPGA spike stimulator, to be used with the Dynap-se board",
    .type = CAER_MODULE_OUTPUT,
    .memSize = sizeof(struct HWFilter_state),
    .functions = &caerPoissonSpikeGenModuleFunctions,
    .inputStreams = moduleInputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
    .outputStreams = NULL,
    .outputStreamsSize = 0
};

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}


static bool caerPoissonSpikeGenModuleInit(caerModuleData moduleData) {

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	size_t inputsSize;
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, &inputsSize);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	// Filter State
	HWFilterState state = moduleData->moduleState;
	state->sourceID = sourceID;

	// Update all settings.
	state->eventSourceConfigNode = caerMainloopGetSourceNode(state->sourceID);
	if (state->eventSourceConfigNode == NULL) {
		return (false);
	}
	state->eventSourceModuleState = caerMainloopGetSourceState(U16T(state->sourceID));

	// create parameters
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "loadBiases", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setSram", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setCam", false);
	sshsNodeCreateBool(moduleData->moduleNode, "Run", false, SSHS_FLAGS_NORMAL, "start stop fpga output");
	sshsNodeCreateBool(moduleData->moduleNode, "Update", false, SSHS_FLAGS_NORMAL, "send parameter update to fpga");
	sshsNodeCreateBool(moduleData->moduleNode, "Load_rates_from_file", false, SSHS_FLAGS_NORMAL, "use file to load mean rates");
	sshsNodeCreateString(moduleData->moduleNode, "Rate_file", "", 0, 1024, SSHS_FLAGS_NORMAL, "input file name");
	sshsNodeCreateInt(moduleData->moduleNode, "Target_neuron_address", 0, 0,255, SSHS_FLAGS_NORMAL, "target neuron id");
	sshsNodeCreateDouble(moduleData->moduleNode, "Rate_Hz", 0, 0, 1000, SSHS_FLAGS_NORMAL, "mean rate of stimulation");
	sshsNodeCreateInt(moduleData->moduleNode, "Chip_ID", 0, 0, 3, SSHS_FLAGS_NORMAL, "destination chip id");
	sshsNodeCreateBool(moduleData->moduleNode, "Program_test_pattern", false, SSHS_FLAGS_NORMAL, "test pattern");

	// update node state
	state->neuronAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Target_neuron_address");
	state->rateHz = sshsNodeGetDouble(moduleData->moduleNode, "Rate_Hz");
	state->run = sshsNodeGetBool(moduleData->moduleNode, "Run");
	state->update = sshsNodeGetBool(moduleData->moduleNode, "Update");
	state->loadRatesFromFile = sshsNodeGetBool(moduleData->moduleNode, "Load_rates_from_file");
	state->rateFile = sshsNodeGetString(moduleData->moduleNode, "Rate_file");
	state->chipID = sshsNodeGetInt(moduleData->moduleNode, "Chip_ID");
	state->programTestPattern = sshsNodeGetBool(moduleData->moduleNode, "Program_test_pattern");

	if (caerStrEquals(state->rateFile, "")) {
		free(state->rateFile);
	}

	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerPoissonSpikeGenModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
    caerEventPacketContainer *out){



}

static void caerPoissonSpikeGenModuleConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	HWFilterState state = moduleData->moduleState;

	// this will update parameters, from user input
	bool newUpdate = sshsNodeGetBool(moduleData->moduleNode, "Update");
	bool newRun = sshsNodeGetBool(moduleData->moduleNode, "Run");
	bool newProgramTestPattern = sshsNodeGetBool(moduleData->moduleNode, "Program_test_pattern");


	// These parameters are always safe to update
	state->loadRatesFromFile = sshsNodeGetBool(moduleData->moduleNode, "Load_rates_from_file");

	// Change run state if necessary
	if (newRun && !state->run) {
		state->run = true;
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_RUN, 1);
		state->chipID = sshsNodeGetInt(moduleData->moduleNode, "Chip_ID");
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, state->chipID);
	}
	else if (!newRun && state->run) {
		state->run = false;
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_RUN,
				    0);
	}

	// Update the poisson rates from file or gui if necessary
	if (newUpdate && !state->update) {
		state->update = true;
		if (state->loadRatesFromFile) {
			// parse the file and update the poisson source rates
			state->rateFile = sshsNodeGetString(moduleData->moduleNode, "Rate_file");
			loadRatesFromFile(moduleData, state->rateFile);
		}
		else {
			// otherwise, just update the single address/rate pair from the gui
			state->neuronAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "Target_neuron_address");
			state->rateHz = sshsNodeGetDouble(moduleData->moduleNode, "Rate_Hz");
			caerDynapseWritePoissonSpikeRate(state->eventSourceModuleState, state->neuronAddr, state->rateHz);
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

	state->chipID = sshsNodeGetInt(moduleData->moduleNode, "Chip_ID");
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_POISSONSPIKEGEN, DYNAPSE_CONFIG_POISSONSPIKEGEN_CHIPID, state->chipID);


	for (uint32_t i = 0; i < DYNAPSE_CONFIG_NUMNEURONS; i++) {
		caerDynapseWriteCam(state->eventSourceModuleState, 0, i, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC );
	}
	caerDynapseWritePoissonSpikeRate(state->eventSourceModuleState, (uint32_t)0, (double)10.0);
}


void loadRatesFromFile(caerModuleData moduleData, char* fileName) {
	HWFilterState state = moduleData->moduleState;

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
		caerDynapseWritePoissonSpikeRate(state->eventSourceModuleState, i, rateArray[i]);
	}
}

static void caerPoissonSpikeGenModuleExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	HWFilterState state = moduleData->moduleState;

	// here we should free memory and other shutdown procedures if needed

}

static void caerPoissonSpikeGenModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	HWFilterState state = moduleData->moduleState;

}
