/*
 * recurrentnet.c
 *
 *  Created on: Capo Caccia 2017
 *      Author: federico
 */

#include "recurrentnet.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/portable_time.h"

struct RNFilter_state {
	// user settings
	bool init;
	float deltaT;
    float period;
    float ieratio;
	// usb utils
	caerInputDynapseState eventSourceModuleState;
	sshsNode eventSourceConfigNode;
};

typedef struct RNFilter_state *RNFilterState;

static struct timespec tstart = { 0, 0 }, tend = { 0, 0 }, ttot = {0,0};

static bool caerRecurrentNetInit(caerModuleData moduleData);
static void caerRecurrentNetRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerRecurrentNetConfig(caerModuleData moduleData);
static void caerRecurrentNetExit(caerModuleData moduleData);
static void caerRecurrentNetReset(caerModuleData moduleData, uint16_t resetCallSourceID);

static struct caer_module_functions caerRecurrentNetFunctions = { .moduleInit =
	&caerRecurrentNetInit, .moduleRun = &caerRecurrentNetRun, .moduleConfig =
	&caerRecurrentNetConfig, .moduleExit = &caerRecurrentNetExit, .moduleReset =
	&caerRecurrentNetReset };

void caerRecurrentNet(uint16_t moduleID, caerSpikeEventPacket spike) {
	caerModuleData moduleData = caerMainloopFindModule(moduleID, "RecurrentNet", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}

	caerModuleSM(&caerRecurrentNetFunctions, moduleData, sizeof(struct RNFilter_state), 1, spike);
}

static bool caerRecurrentNetInit(caerModuleData moduleData) {
	// create parameters
	sshsNodePutFloatIfAbsent(moduleData->moduleNode, "deltaT", 2.0);
    sshsNodePutFloatIfAbsent(moduleData->moduleNode, "period", 3.0);
    sshsNodePutFloatIfAbsent(moduleData->moduleNode, "ieratio", 0.4);

	RNFilterState state = moduleData->moduleState;

	// update node state
	state->deltaT = sshsNodeGetFloat(moduleData->moduleNode, "deltaT");
    state->period = sshsNodeGetFloat(moduleData->moduleNode, "period");
    state->ieratio = sshsNodeGetFloat(moduleData->moduleNode, "ieratio");

    
	state->init = false;

	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	portable_clock_gettime_monotonic(&tstart);
	portable_clock_gettime_monotonic(&ttot);

	// Nothing that can fail here.
	return (true);
}

static void caerRecurrentNetRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).
	caerSpikeEventPacket spike = va_arg(args, caerSpikeEventPacket);

	// Only process packets with content.
	if (spike == NULL) {
		return;
	}

	RNFilterState state = moduleData->moduleState;

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


	if(state->init == false){
		// do the initialization

		caerLog(CAER_LOG_NOTICE, __func__, "Initialization of the Recurrent Network");

		// stimulation chip
		caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U1, 0, "IF_RFR_N", 2, 24, "LowBias", "NBias");


		// load biases
		for(size_t coreid=0; coreid<4 ; coreid++){
            caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_DC_P", 7, 2, "HighBias", "PBias");
            //caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_DC_P", 7, 1, "HighBias", "PBias");
            caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_DC_P", 7, 2, "HighBias", "PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_DC_P", 7, 2, "HighBias", "PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_THR_N", 3, 20, "HighBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_TAU1_N", 6, 21, "HighBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHTAU_N", 7, 35, "LowBias", "NBias");

			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHTAU_N", 7, 35, "LowBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHTHR_N", 7, 1, "HighBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHW_P", 7, 1, "HighBias", "PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_BUF_P", 3, 80, "HighBias", "PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_CASC_N", 7, 1, "HighBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_DC_P", 7, 2, "HighBias", "PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_NMDA_N", 7, 1, "HighBias", "PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_RFR_N", 4, 208, "HighBias", "NBias");

			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_TAU1_N", 6, 21, "LowBias", "NBias");

			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_TAU2_N", 5, 15, "HighBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_THR_N", 3, 20, "HighBias", "NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_TAU_F_P", 5, 53, "HighBias",
				"PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_TAU_S_P", 7, 40, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_THR_F_P", 2, 200, "HighBias",
				"PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_THR_S_P", 7, 0, "HighBias",
				"PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_TAU_F_P", 7, 40, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_TAU_S_P", 7, 40, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_THR_F_P", 7, 40, "HighBias",
				"PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_THR_S_P", 7, 40, "HighBias",
				"PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_EXC_F_N", 1, 90, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_EXC_S_N", 7, 1, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_INH_F_N", 0, 100, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_INH_S_N", 7, 1, "HighBias",
				"NBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PULSE_PWLK_P", 0, 43, "HighBias",
				"PBias");
			caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "R2R_P", 4, 85, "HighBias", "PBias");

		}

		// --- set sram
		//  0 - select which chip to configure
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);

        
        int i, j;
        bool v[1024];
        
        for(i=0; i<1024; i++) { // Target neurons
            
            for(j=0; j<1024; j++) { v[j] = false; }

            // Update sram of source neuron
            caerDynapseWriteSram(stateSource->deviceState, i/256, i % 256, i/256,
                                 DYNAPSE_CONFIG_SRAM_DIRECTION_X_WEST, 0,
                                 DYNAPSE_CONFIG_SRAM_DIRECTION_Y_NORTH, 0,
                                 1,  // SRAM ID 1 (0 is reserved for USB)
                                 15); // 1111, all cores

            for(j=0; j<64; j++) {

                // Sample unique source neuron
                int index;
                do {
                    index = rand() % 1024;
                } while(v[index]==true);
                v[index] = true;

                // Update cam of destination neuron
                if(rand() < RAND_MAX*state->ieratio) {
                    caerDynapseWriteCam(stateSource->deviceState, index, i, j, DYNAPSE_CONFIG_CAMTYPE_F_INH);
                } else {
                    caerDynapseWriteCam(stateSource->deviceState, index, i, j, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
                }
            }
        }
        
		caerLog(CAER_LOG_NOTICE, __func__, "init completed");


		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U1);
	    caerDynapseWriteSram(stateSource->deviceState, 0, 0, 0,
	                         1, 1,
	                         0, 0,
	                         1,  // SRAM ID 1 (0 is reserved for USB)
	                         15); // 1111, all cores
	    caerDynapseWriteSram(stateSource->deviceState, 0, 1, 0,
	                         1, 1,
	                         0, 0,
	                         1,  // SRAM ID 1 (0 is reserved for USB)
	                         15); // 1111, all cores
	    caerDynapseWriteSram(stateSource->deviceState, 0, 2, 0,
	                         1, 1,
	                         0, 0,
	                         1,  // SRAM ID 1 (0 is reserved for USB)
	                         15); // 1111, all cores
	    caerDynapseWriteSram(stateSource->deviceState, 0, 3, 0,
	                         1, 1,
	                         0, 0,
	                         1,  // SRAM ID 1 (0 is reserved for USB)
	                         15); // 1111, all cores
		state->init = true;
	}

	portable_clock_gettime_monotonic(&tend);
	double current_time = (double) (((double) tend.tv_sec + 1.0e-9 * tend.tv_nsec) - ((double) tstart.tv_sec
			+ 1.0e-9 * tstart.tv_nsec));

	// let's change here the stimulation pattern
	if(current_time >= state->deltaT){
		portable_clock_gettime_monotonic(&tstart);
        portable_clock_gettime_monotonic(&ttot);
		int coreid = 0;
		int tt = (int) ((sin( (double)(6.2832/state->period)*(ttot.tv_sec + 1.0e-9* ttot.tv_nsec))+1.0 )*127);
		caerLog(CAER_LOG_NOTICE, __func__, "tt %d", tt);
		//caerDynapseSetBias(stateSource, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_DC_P", 5, tt, "HighBias", "PBias");

		uint32_t bits = generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode,
			"C0_IF_DC_P", 5, tt, "HiBias", "Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);

		caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, bits);

	}

	// Iterate over spikes in the packet
	/*CAER_SPIKE_ITERATOR_VALID_START(spike)
		int32_t timestamp =  caerSpikeEventGetTimestamp(caerSpikeIteratorElement);
	  	uint8_t chipid 	  =  caerSpikeEventGetChipID(caerSpikeIteratorElement);
	  	uint8_t neuronid  =  caerSpikeEventGetNeuronID(caerSpikeIteratorElement);
	  	uint8_t coreid    =  caerSpikeEventGetSourcecoreid(caerSpikeIteratorElement);


	CAER_SPIKE_ITERATOR_VALID_END*/


  	// sending bits to the USB, for
	// programing sram content
  	// sending spikes
	//caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	//caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, bits);

  	// send to usb in packets
  	// bool caerDynapseSendDataToUSB(caerDeviceHandle handle, int * data, int numConfig);

}

static void caerRecurrentNetConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	RNFilterState state = moduleData->moduleState;

	// this will update parameters, from user input
    state->deltaT = sshsNodeGetFloat(moduleData->moduleNode, "deltaT");
    state->period = sshsNodeGetFloat(moduleData->moduleNode, "period");
    state->ieratio = sshsNodeGetFloat(moduleData->moduleNode, "ieratio");

}

static void caerRecurrentNetExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	RNFilterState state = moduleData->moduleState;

	// here we should free memory and other shutdown procedures if needed

}

static void caerRecurrentNetReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	RNFilterState state = moduleData->moduleState;

}
