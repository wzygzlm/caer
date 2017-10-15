#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/devices/dynapse.h>
#include <libcaer/events/spike.h>

struct SynapseReconfig_state {
	uint32_t chipSelect;
	uint32_t sramBaseAddr;
	bool useSramKernels;
	bool runDvs;
	bool updateSramKernels;
	char* globalKernelFilePath;
	char* sramKernelFilePath;
	bool doInit;
	caerDeviceHandle eventSourceModuleState;
	sshsNode eventSourceModuleNode;
};

typedef struct SynapseReconfig_state *SynapseReconfigState;        // filter state contains two variables

// this are required functions

// Init will start at startup and will be executed only once

static bool caerSynapseReconfigModuleInit(caerModuleData moduleData);

// run will be called in the mainloop (it is where the processing of the events happens)

static void caerSynapseReconfigModuleRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);

// to configure the filter

static void caerSynapseReconfigModuleConfig(caerModuleData moduleData);

// called at exit , usually used to free memory etc..

static void caerSynapseReconfigModuleExit(caerModuleData moduleData);

// to reset the filter/module in a default state

static void caerSynapseReconfigModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);

void updateChipSelect(caerModuleData moduleData);
void updateGlobalKernelData(caerModuleData moduleData);
void updateSramKernelData(caerModuleData moduleData);

static struct caer_module_functions caerSynapseReconfigModuleFunctions = { .moduleInit =

									   &caerSynapseReconfigModuleInit, .moduleRun = &caerSynapseReconfigModuleRun, .moduleConfig =

									   &caerSynapseReconfigModuleConfig, .moduleExit = &caerSynapseReconfigModuleExit, .moduleReset =

									   &caerSynapseReconfigModuleReset };


static const struct caer_event_stream_in moduleInputs[] = {
    { .type = SPIKE_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "SynapseReconfig",
	.description = "Davis240C to dynapse processor mapping",
	.type = CAER_MODULE_OUTPUT,
	.memSize = sizeof(struct SynapseReconfig_state),
	.functions = &caerSynapseReconfigModuleFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL,
	.outputStreamsSize = 0
};

// init

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}

static bool caerSynapseReconfigModuleInit(caerModuleData moduleData) {

	SynapseReconfigState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	// get source state
	state->eventSourceModuleState = caerMainloopGetSourceState(sourceID);
	state->eventSourceModuleNode = caerMainloopGetSourceNode(sourceID);

	// add parameters for the user

	sshsNodeCreateBool(moduleData->moduleNode, "runDVS", false, SSHS_FLAGS_NORMAL, "Start/Stop mapping");
	sshsNodeCreateBool(moduleData->moduleNode, "useSRAMKernels", false, SSHS_FLAGS_NORMAL, "Use Sram Kernel file");
	sshsNodeCreateInt(moduleData->moduleNode, "SRAMBaseAddress", 0, 0, 1, SSHS_FLAGS_NORMAL, "Sram base address");
	sshsNodeCreateInt(moduleData->moduleNode, "targetChipID", 0, 0, 3, SSHS_FLAGS_NORMAL, "Sram base address");
	sshsNodeCreateString(moduleData->moduleNode, "globalKernelFilePath", "", 0, 2048, SSHS_FLAGS_NORMAL, "Global Sram kernel file path, relative from the folder in which caer is started");
	sshsNodeCreateString(moduleData->moduleNode, "SRAMKernelFilePath", "", 0, 2048, SSHS_FLAGS_NORMAL, "Sram kernels file path, relative from the folder in which caer is started");
	sshsNodeCreateBool(moduleData->moduleNode, "updateSRAMKernels", false, SSHS_FLAGS_NORMAL, "Perform update of Sram content from file");

	// put the parameters in the state of the filter

	state->runDvs = sshsNodeGetBool(moduleData->moduleNode, "runDVS");
	state->chipSelect = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "targetChipID");
	state->sramBaseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "SRAMBaseAddress");
	state->useSramKernels = sshsNodeGetBool(moduleData->moduleNode, "useSRAMKernels");
	state->updateSramKernels = sshsNodeGetBool(moduleData->moduleNode, "updateSRAMKernels");
	state->sramKernelFilePath = sshsNodeGetString(moduleData->moduleNode, "SRAMKernelFilePath");
	state->globalKernelFilePath = sshsNodeGetString(moduleData->moduleNode, "globalKernelFilePath");

	if ( caerStrEquals(state->sramKernelFilePath, "") ) {
		free(state->sramKernelFilePath);
	}
	if ( caerStrEquals(state->globalKernelFilePath, "") ) {
		free(state->globalKernelFilePath);
	}

	// do init false - initialization clear cam and load biases -
	state->doInit = true;

	// Add config listeners last - let's the user interact with the parameter -

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);


	// Nothing that can fail here.

	return (true);

}

// the actual processing function

static void caerSynapseReconfigModuleRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerSpikeEventPacketConst spike =
		(caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, SPIKE_EVENT);

	// Only process packets with content.
	if (spike == NULL) {
		return;
	}

	// short cut

	SynapseReconfigState state = moduleData->moduleState;
	if(state->doInit){
		// do init
		// clear CAMs and load default biases
		sshsNode camControlNode = sshsGetRelativeNode(state->eventSourceModuleNode, "CAM/");
		sshsNodePutBool(camControlNode, "EmptyAll", true);

		sshsNode biasNode = sshsGetRelativeNode(state->eventSourceModuleNode, "bias/");
		sshsNodePutBool(biasNode, "ResetAllBiasesToDefault", true);

		// do not do init anymore
		state->doInit = false;
	}
}

// update parameters

static void caerSynapseReconfigModuleConfig(caerModuleData moduleData) {

	caerModuleConfigUpdateReset(moduleData);

	SynapseReconfigState state = moduleData->moduleState;

	// We don't know what changed when this function is called so we will check and only update
	// when run/stop or "use SRAM kernels" changes.
	bool newRunDvs = sshsNodeGetBool(moduleData->moduleNode, "runDVS");
	uint8_t newChipSelect = (uint8_t)sshsNodeGetInt(moduleData->moduleNode, "targetChipID");

	state->updateSramKernels = sshsNodeGetBool(moduleData->moduleNode, "updateSRAMKernels");

	state->useSramKernels = sshsNodeGetBool(moduleData->moduleNode, "useSRAMKernels");
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_USESRAMKERNELS, state->useSramKernels);

	// Only update other values when toggling run/stop mode
	if ( newRunDvs && !state->runDvs ) {

		// update chip select if it changed
		if ( state->chipSelect != newChipSelect ) {
			state->chipSelect = newChipSelect;
			updateChipSelect(moduleData);

		}
	    
		// Update the global kernel whenever we enable the DVS
		state->globalKernelFilePath = sshsNodeGetString(moduleData->moduleNode, "globalKernelFilePath");
		updateGlobalKernelData(moduleData);
		
		// Only update the SRAM kernels if asked since it takes 5 seconds
		if (state->updateSramKernels) {
			state->sramKernelFilePath = sshsNodeGetString(moduleData->moduleNode, "SRAMKernelFilePath");
			state->sramBaseAddr = (uint32_t)sshsNodeGetInt(moduleData->moduleNode, "SRAMBaseAddress");
			updateSramKernelData(moduleData);
		}

		// finally update the DVS run status
		state->runDvs = newRunDvs;
		// start up the DVS with default kernel
		caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Enabling DVS...\n");
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_RUN, true);
		uint32_t param = 0;
		caerDeviceConfigGet(state->eventSourceModuleState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_RUN, &param);
		if ( param == 0 ) {
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Failed to enable DVS chain because value was never written\n");
		}
		else if ( param == 1 ) {
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Enabled DVS chain\n");
		}

	}
	else if ( !newRunDvs && state->runDvs ) {
		state->runDvs = newRunDvs;
		// disable the DVS
		caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Disabling DVS-to-Dynapse...\n");
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_RUN, false);
	}


}

// clean memory - nothing really in this simple example

static void caerSynapseReconfigModuleExit(caerModuleData moduleData) {

	// Remove listener, which can reference invalid memory in userData.

	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	SynapseReconfigState state = moduleData->moduleState;

	// here we should free memory and other shutdown procedures if needed

}

// reset

static void caerSynapseReconfigModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {

	UNUSED_ARGUMENT(resetCallSourceID);

	SynapseReconfigState state = moduleData->moduleState;

}

void updateChipSelect(caerModuleData moduleData) {
	SynapseReconfigState state = moduleData->moduleState;

	caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Selecting chip U%d.", state->chipSelect);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SYNAPSERECONFIG,
		DYNAPSE_CONFIG_SYNAPSERECONFIG_CHIPSELECT, state->chipSelect);
}
void updateGlobalKernelData(caerModuleData moduleData) {
	SynapseReconfigState state = moduleData->moduleState;

	// Load kernel data from a formatted file consisting of 2 8x8 matrix with comma separated values
	// The first 8 rows are the kernel entries for incoming positive events and the last 8 rows for negative events.
	FILE* fp = fopen(state->globalKernelFilePath, "r");

	if ( fp == NULL ) {
		caerLog(CAER_LOG_NOTICE, __func__, "Could not open global kernel file\n");
	} else {
		//read the kernel file
		int positiveKernel[8][8] = {0};
		int negativeKernel[8][8] = {0};

		for ( int i = 0; i < 8; i++ ) {
			for ( int k = 0; k < 7; k++ ) {
				fscanf(fp, "%d, ", &positiveKernel[i][k]);
			}
			fscanf(fp, "%d\n", &positiveKernel[i][7]);
		}
		for ( int i = 0; i < 8; i++ ) {
			for ( int k = 0; k < 7; k++ ) {
				fscanf(fp, "%d, ", &negativeKernel[i][k]);
			}
			fscanf(fp, "%d\n", &negativeKernel[i][7]);
		}

		// transform to excitatory/inhibitory fast/slow or 0 form
		for ( int i = 0; i < 8; i++ ) {
			for ( int k = 0; k < 8; k++ ) {
				switch ( positiveKernel[i][k] ) {
				case -2:
					positiveKernel[i][k] = 0x04;
					break;
				case -1:
					positiveKernel[i][k] = 0x05;
					break;
				case 0:
					positiveKernel[i][k] = 0x00;
					break;
				case 1:
					positiveKernel[i][k] = 0x07;
					break;
				case 2:
					positiveKernel[i][k] = 0x06;
					break;
				default:
					break;
				}
				switch ( negativeKernel[i][k] ) {
				case -2:
					negativeKernel[i][k] = 0x04;
					break;
				case -1:
					negativeKernel[i][k] = 0x05;
					break;
				case 0:
					negativeKernel[i][k] = 0x00;
					break;
				case 1:
					negativeKernel[i][k] = 0x07;
					break;
				case 2:
					negativeKernel[i][k] = 0x06;
					break;
				default:
					break;
				}
			}
		}

		// program the kernel
		// Data is encoded as 12 bit in a 16 bit word with the format
		// | negative event synapse n+1 | positive event synapse n+1 | negative event synapse n | positive event synapse n |

		for ( uint32_t i = 0; i < 8; i++ ) {
			for ( uint32_t k = 0; k < 8; k+=2 ) {
				uint32_t var = (uint32_t)positiveKernel[i][k] | // synapse 1 positive
					(uint32_t)negativeKernel[i][k] << 3 | // synapse 1 negative
					(uint32_t)positiveKernel[i][k+1] << 6 | // synapse 2 positive
					(uint32_t)negativeKernel[i][k+1] << 9; // synapse 2 negative

				// address goes in front of the data in the same word for global kernel programming
				var |= ( i * 4 + k / 2 ) << 12; 

				caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_SYNAPSERECONFIG,
						    DYNAPSE_CONFIG_SYNAPSERECONFIG_GLOBALKERNEL, var);
				caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "DVSChain global config file: %#08X", var);
			}
		}
	}

	fclose(fp);
}
void updateSramKernelData(caerModuleData moduleData) {
	SynapseReconfigState state = moduleData->moduleState;
	FILE* fp;

	// Read SRAM kernels from a file formatted with 1024 rows of 128 entries alternating between
	// event codes for first positive then negative DVS events.
	// Event codes: -2 = inhibitory slow, -1 = inhibitory fast, 0 = no events will be applied,
	// 1 = fast excitatory, 2 = slow excitatory
	fp = fopen(state->sramKernelFilePath, "r");

	if (fp == NULL) {
		caerLog(CAER_LOG_NOTICE, __func__, "Could not open SRAM kernel file\n");
	}
	else {
		uint16_t sramTable[1024*32] = {0};
		for (int i = 0; i < 1024; i++) {
			for (int j = 0; j < 31; j++) {
				int intCode[4] = {0};
				// read values for 2 synapses which will fit in 1 sram word
				// each synapse has a weight for a positive and negative DVS event
				// which is a total of 4 weights in one sram word
				fscanf(fp, "%d, %d, %d, %d,", &intCode[0], &intCode[1], &intCode[2], &intCode[3]);
				for (int k = 0; k < 4; k++) {
					switch ( intCode[k] ) {
					case -2:
						sramTable[i * 32 + j] |= 0x04 << (k * 3);
						break;
					case -1:
						sramTable[i * 32 + j] |= 0x05 << (k * 3);
						break;
					case 0:
						sramTable[i * 32 + j] |= 0x00 << (k * 3);
						break;
					case 1:
						sramTable[i * 32 + j] |= 0x07 << (k * 3);
						break;
					case 2:
						sramTable[i * 32 + j] |= 0x06 << (k * 3);
						break;
					default:
						break;
					}
				}
			}

			// handle end of csv formatted line
			int intCode[4] = {0};
			fscanf(fp, "%d, %d, %d, %d\n", &intCode[0], &intCode[1], &intCode[2], &intCode[3]);
			for (int k = 0; k < 4; k++) {
				switch ( intCode[k] ) {
				case -2:
					sramTable[i * 32 + 31] |= 0x04 << (k * 3);
					break;
				case -1:
					sramTable[i * 32 + 31] |= 0x05 << (k * 3);
					break;
				case 0:
					sramTable[i * 32 + 31] |= 0x00 << (k * 3);
					break;
				case 1:
					sramTable[i * 32 + 31] |= 0x07 << (k * 3);
					break;
				case 2:
					sramTable[i * 32 + 31] |= 0x06 << (k * 3);
					break;
				default:
					break;
				}
			}
		}

		caerLog(CAER_LOG_NOTICE, __func__, "Writing SRAM kernel table... ");
		caerDynapseWriteSramWords(state->eventSourceModuleState, sramTable, state->sramBaseAddr << 15, 1024*32);
		caerLog(CAER_LOG_NOTICE, __func__, "Done!\n");
	}
				
	fclose(fp);
}
