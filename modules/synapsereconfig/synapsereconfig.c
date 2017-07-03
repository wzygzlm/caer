#include "synapsereconfig.h"
#include "base/mainloop.h"
#include "base/module.h"
#include <libcaer/devices/dynapse.h>
#include "modules/ini/dynapse_common.h"

struct SynapseReconfig_state {
	int sourceID;
	uint32_t chipSelect;
	uint32_t sramBaseAddr;
	bool useSramKernels;
	bool runDvs;
	bool updateSramKernels;
	char* globalKernelFilePath;
	char* sramKernelFilePath;
	bool doInit;
	caerInputDynapseState eventSourceModuleState;
};

typedef struct SynapseReconfig_state *SynapseReconfigState;        // filter state contains two variables

// this are required functions

// Init will start at startup and will be executed only once

static bool caerSynapseReconfigModuleInit(caerModuleData moduleData);

// run will be called in the mainloop (it is where the processing of the events happens)

static void caerSynapseReconfigModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args);

// to configure the filter

static void caerSynapseReconfigModuleConfig(caerModuleData moduleData);

// called at exit , usually used to free memory etc..

static void caerSynapseReconfigModuleExit(caerModuleData moduleData);

// to reset the filter/module in a default state

static void caerSynapseReconfigModuleReset(caerModuleData moduleData, uint16_t resetCallSourceID);

void updateChipSelect(caerModuleData moduleData);
void updateGlobalKernelData(caerModuleData moduleData);
void updateSramKernelData(caerModuleData moduleData);

static struct caer_module_functions caerSynapseReconfigModuleFunctions = { .moduleInit =

									   &caerSynapseReconfigModuleInit, .moduleRun = &caerSynapseReconfigModuleRun, .moduleConfig =

									   &caerSynapseReconfigModuleConfig, .moduleExit = &caerSynapseReconfigModuleExit, .moduleReset =

									   &caerSynapseReconfigModuleReset };


int global_sourceID;

void caerSynapseReconfigModule(uint16_t moduleID, caerSpikeEventPacket spike) {

	caerModuleData moduleData = caerMainloopFindModule(moduleID, "Synapse-Reconfig", CAER_MODULE_PROCESSOR);

	if (moduleData == NULL) {

		return;

	}

	caerModuleSM(&caerSynapseReconfigModuleFunctions, moduleData, sizeof(struct SynapseReconfig_state), 1, spike);

}

// init

static bool caerSynapseReconfigModuleInit(caerModuleData moduleData) {

	// add parameters for the user

	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "runDVS", false);
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "useSRAMKernels", false);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "SRAMBaseAddress", 0);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "targetChipID", 0);
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "globalKernelFilePath", "");
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "SRAMKernelFilePath", "");
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "updateSRAMKernels", false);

	SynapseReconfigState state = moduleData->moduleState;

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

	// do init false - initialiaztion clear cam and load biases -
	state->doInit = true;

	// Add config listeners last - let's the user interact with the parameter -

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.

	return (true);

}

// the actual processing function

static void caerSynapseReconfigModuleRun(caerModuleData moduleData, size_t argsNumber, va_list args) {

	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).

	// in this case we only get spike events

	caerSpikeEventPacket spike = va_arg(args, caerSpikeEventPacket);

	// Only process packets with content.

	if (spike == NULL) {

		return;

	}

	// short cut

	SynapseReconfigState state = moduleData->moduleState;

	global_sourceID = caerEventPacketHeaderGetEventSource(&spike->packetHeader);
    
	if(state->doInit){
		// do init
		// --- start  usb handle / from spike event source id
		state->eventSourceModuleState = caerMainloopGetSourceState(U16T(global_sourceID));
		if (state->eventSourceModuleState == NULL) {
			return;
		}
		caerInputDynapseState stateSource = state->eventSourceModuleState;
		if (stateSource->deviceState == NULL) {
			return;
		}
		// --- end usb handle
		// clear CAM al load default biases
		atomic_store(&state->eventSourceModuleState->genSpikeState.clearAllCam, true);
		atomic_store(&state->eventSourceModuleState->genSpikeState.loadDefaultBiases, true);
		// do not do init anymore
		state->doInit = false;
	}

}

// update parameters

static void caerSynapseReconfigModuleConfig(caerModuleData moduleData) {
	caerInputDynapseState eventSource = caerMainloopGetSourceState(U16T(global_sourceID)); 

	caerModuleConfigUpdateReset(moduleData);

	SynapseReconfigState state = moduleData->moduleState;


	// We don't know what changed when this function is called so we will check and only update
	// when run/stop or "use SRAM kernels" changes.
	bool newRunDvs = sshsNodeGetBool(moduleData->moduleNode, "runDVS");
	uint8_t newChipSelect = (uint8_t)sshsNodeGetInt(moduleData->moduleNode, "targetChipID");

	state->updateSramKernels = sshsNodeGetBool(moduleData->moduleNode, "updateSRAMKernels");

	state->useSramKernels = sshsNodeGetBool(moduleData->moduleNode, "useSRAMKernels");
	caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_USESRAMKERNELS, state->useSramKernels);

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
		caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_RUN, true);
		uint32_t param = 0;
		caerDeviceConfigGet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_RUN, &param);
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
		caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_RUN, false);
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

static void caerSynapseReconfigModuleReset(caerModuleData moduleData, uint16_t resetCallSourceID) {

	UNUSED_ARGUMENT(resetCallSourceID);

	SynapseReconfigState state = moduleData->moduleState;

}

void updateChipSelect(caerModuleData moduleData) {
	SynapseReconfigState state = moduleData->moduleState;
	caerInputDynapseState eventSource = caerMainloopGetSourceState(U16T(global_sourceID)); 

	switch ( state->chipSelect ) {
	case DYNAPSE_CONFIG_DYNAPSE_U0:
		caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Selecting chip U0\n");
		caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_CHIPSELECT,
				    DYNAPSE_CONFIG_DYNAPSE_U0);
		break;
	case DYNAPSE_CONFIG_DYNAPSE_U1:
		caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Selecting chip U1\n");
		caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_CHIPSELECT,
				    DYNAPSE_CONFIG_DYNAPSE_U1);
		break;
	case DYNAPSE_CONFIG_DYNAPSE_U2:
		caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Selecting chip U2\n");
		caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_CHIPSELECT,
				    DYNAPSE_CONFIG_DYNAPSE_U2);
		break;
	case DYNAPSE_CONFIG_DYNAPSE_U3:
		caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Selecting chip U3\n");
		caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG, DYNAPSE_CONFIG_SYNAPSERECONFIG_CHIPSELECT,
				    DYNAPSE_CONFIG_DYNAPSE_U3);
		break;
	default:
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Chip select invalid\n");
		break;
	}

}
void updateGlobalKernelData(caerModuleData moduleData) {
	SynapseReconfigState state = moduleData->moduleState;
	caerInputDynapseState eventSource = caerMainloopGetSourceState(U16T(global_sourceID)); 

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

				caerDeviceConfigSet(eventSource->deviceState, DYNAPSE_CONFIG_SYNAPSERECONFIG,
						    DYNAPSE_CONFIG_SYNAPSERECONFIG_GLOBALKERNEL, var);
				caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "DVSChain global config file: %#08X", var);
			}
		}
	}

	fclose(fp);
}
void updateSramKernelData(caerModuleData moduleData) {
	SynapseReconfigState state = moduleData->moduleState;
	caerInputDynapseState eventSource = caerMainloopGetSourceState(U16T(global_sourceID)); 
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
		caerDynapseWriteSramWords(eventSource->deviceState, sramTable, state->sramBaseAddr << 15, 1024*32);
		caerLog(CAER_LOG_NOTICE, __func__, "Done!\n");
	}
				
	fclose(fp);
}
