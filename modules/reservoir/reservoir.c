/*
 *
 *  Created on: August, 2017
 *      Author: federico.corradi@inilabs.com
 */

#include <time.h>
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include "ext/portable_time.h"
#include <libcaer/devices/dynapse.h>
#include "ext/colorjet/colorjet.h"
#include "modules/ini/dynapse_common.h"

struct RSFilter_state {
	caerInputDynapseState eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	int16_t sourceID;
	// user settings
	bool init;
	bool setbias;
	float deltaT;
	float period;
	float ieratio;
};

typedef struct RSFilter_state *RSFilterState;
static struct timespec tstart = { 0, 0 }, tend = { 0, 0 }, ttot = { 0, 0 };

static bool caerReservoirInit(caerModuleData moduleData);
static void caerReservoirRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerReservoirConfig(caerModuleData moduleData);
static void caerReservoirExit(caerModuleData moduleData);
static void caerReservoirReset(caerModuleData moduleData, uint16_t resetCallSourceID);

static struct caer_module_functions caerReservoirFunctions = { .moduleInit = &caerReservoirInit, .moduleRun =
	&caerReservoirRun, .moduleConfig = &caerReservoirConfig, .moduleExit = &caerReservoirExit, .moduleReset =
	&caerReservoirReset };

static const struct caer_event_stream_in moduleInputs[] = { { .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "Reservoir", .description =
	"Reservoir of neurons", .type = CAER_MODULE_OUTPUT, .memSize = sizeof(struct RSFilter_state), .functions =
	&caerReservoirFunctions, .inputStreams = moduleInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL, .outputStreamsSize = NULL };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

static bool caerReservoirInit(caerModuleData moduleData) {

	RSFilterState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	state->sourceID = inputs[0];
	free(inputs);

	// create user parameters
	sshsNodeCreateFloat(moduleData->moduleNode, "deltaT", 1.0, 0.0, 100.0, SSHS_FLAGS_NORMAL, "DeltaT");
	sshsNodeCreateFloat(moduleData->moduleNode, "period", 0.1, 0.0, 1.0, SSHS_FLAGS_NORMAL, "Period");
	sshsNodeCreateFloat(moduleData->moduleNode, "ieratio", 5, 0.0, 10.0, SSHS_FLAGS_NORMAL,
		"Excitatory to inhibitory connectivity ratio");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodeCreateShort(sourceInfoNode, "dataSizeX", DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX,
		DYNAPSE_X4BOARD_NEUX * 16, SSHS_FLAGS_NORMAL, "number of neurons in X");
		sshsNodeCreateShort(sourceInfoNode, "dataSizeY", DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY,
		DYNAPSE_X4BOARD_NEUY * 16, SSHS_FLAGS_NORMAL, "number of neurons in Y");
	}

	// internals
	state->init = false;
	state->setbias = false;

	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	portable_clock_gettime_monotonic(&tstart);
	portable_clock_gettime_monotonic(&ttot);

	// Nothing that can fail here.
	return (true);
}

static void caerReservoirRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerSpikeEventPacketConst spike = (caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in,
		SPIKE_EVENT);

	RSFilterState state = moduleData->moduleState;

	// now we can do crazy processing etc..
	// first find out which one is the module producing the spikes. and get usb handle
	// --- start  usb handle / from spike event source id
	int sourceID = caerEventPacketHeaderGetEventSource(&spike->packetHeader);
	state->eventSourceModuleState = caerMainloopGetSourceState(U16T(sourceID));
	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(sourceID));
	if (state->eventSourceModuleState == NULL || state->eventSourceConfigNode == NULL) {
		return;
	}
	caerInputDynapseState stateSource = state->eventSourceModuleState;
	if (stateSource->deviceState == NULL) {
		return;
	}
	// --- end usb handle

	if (state->init == false) {
		// do the initialization

		caerLog(CAER_LOG_NOTICE, __func__, "Initialization of the Reservoir Network");

		// load biases
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_DC_P", 7, 0, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);

		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U1);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U2);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_THR_N", 7, 0, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U3);

		// --- set sram
		//  0 - select which chip to configure
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U0);

		int i, j, index, num_exc = 0, num_inh = 0;
		bool v[1024];
		uint32_t bits[1024];
		int numConfig = -1;

		for (i = 0; i < 1024; i++) {
			// Update sram of source neuron
			caerDynapseWriteSram(stateSource->deviceState, i / 256, i % 256, i / 256, 0, 0, 0, 0, 1, // SRAM ID 1 (0 is reserved for USB)
				15); // 1111, all cores
		}

		for (i = 0; i < 1024; i++) { // Target neurons

			for (j = 0; j < 1024; j++) {
				v[j] = false;
			}
			v[284] = true;

			for (j = 0; j < 64; j++) {

				// Sample unique source neuron

				do {
					index = rand() % 1250;
					index = index % 1024;
				}
				while (v[index] == true);
				v[index] = false;
				numConfig++;
				if (numConfig >= 1024) {
					caerDynapseSendDataToUSB(stateSource->deviceState, bits, numConfig);
					numConfig = 0;

				}

				// Update cam of destination neuron
				if (rand() % 100 <= state->ieratio) {
					bits[numConfig] = caerDynapseGenerateCamBits(index, i, j, DYNAPSE_CONFIG_CAMTYPE_S_INH);
					num_inh++;
				}
				else {
					bits[numConfig] = caerDynapseGenerateCamBits(index, i, j, DYNAPSE_CONFIG_CAMTYPE_S_EXC);
					num_exc++;
				}
			}

		}
		caerDynapseSendDataToUSB(stateSource->deviceState, bits, numConfig);
		caerLog(CAER_LOG_NOTICE, __func__, "inh num: %i", num_inh);
		caerLog(CAER_LOG_NOTICE, __func__, "exc num: %i", num_exc);

		// load biases
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_DC_P", 7, 200, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_AHTAU_N", 7, 35, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_AHTHR_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_AHW_P", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_BUF_P", 3, 80, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_CASC_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_NMDA_N", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_RFR_N", 3, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_RFR_N", 0, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_TAU1_N", 7, 0, "LowBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_TAU2_N", 7, 15, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_IF_THR_N", 1, 250, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPIE_TAU_F_P", 2, 53, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPIE_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPIE_THR_F_P", 2, 200, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPIE_THR_S_P", 7, 0, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPII_TAU_F_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPII_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPII_THR_F_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_NPDPII_THR_S_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_PS_WEIGHT_EXC_F_N", 1, 90, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_PS_WEIGHT_EXC_S_N", 0, 255, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_PS_WEIGHT_INH_F_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_PS_WEIGHT_INH_S_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_PULSE_PWLK_P", 0, 43, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C0_R2R_P", 4, 85, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);

		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_DC_P", 7, 200, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_AHTAU_N", 7, 35, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_AHTHR_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_AHW_P", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_BUF_P", 3, 80, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_CASC_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_NMDA_N", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_RFR_N", 3, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_RFR_N", 0, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_TAU1_N", 7, 0, "LowBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_TAU2_N", 7, 15, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_IF_THR_N", 1, 250, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPIE_TAU_F_P", 2, 53, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPIE_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPIE_THR_F_P", 2, 200, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPIE_THR_S_P", 7, 0, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPII_TAU_F_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPII_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPII_THR_F_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_NPDPII_THR_S_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_PS_WEIGHT_EXC_F_N", 1, 90, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_PS_WEIGHT_EXC_S_N", 0, 255, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_PS_WEIGHT_INH_F_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_PS_WEIGHT_INH_S_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_PULSE_PWLK_P", 0, 43, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C1_R2R_P", 4, 85, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);

		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_DC_P", 7, 200, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_AHTAU_N", 7, 35, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_AHTHR_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_AHW_P", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_BUF_P", 3, 80, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_CASC_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_NMDA_N", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_RFR_N", 3, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_RFR_N", 0, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_TAU1_N", 7, 0, "LowBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_TAU2_N", 7, 15, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_IF_THR_N", 1, 250, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPIE_TAU_F_P", 2, 53, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPIE_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPIE_THR_F_P", 2, 200, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPIE_THR_S_P", 7, 0, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPII_TAU_F_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPII_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPII_THR_F_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_NPDPII_THR_S_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_PS_WEIGHT_EXC_F_N", 1, 90, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_PS_WEIGHT_EXC_S_N", 0, 255, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_PS_WEIGHT_INH_F_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_PS_WEIGHT_INH_S_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_PULSE_PWLK_P", 0, 43, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C2_R2R_P", 4, 85, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);

		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_DC_P", 7, 200, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_AHTAU_N", 7, 35, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_AHTHR_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_AHW_P", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_BUF_P", 3, 80, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_CASC_N", 7, 1, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_NMDA_N", 7, 1, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_RFR_N", 3, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_RFR_N", 0, 248, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_TAU1_N", 7, 0, "LowBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_TAU2_N", 7, 15, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_IF_THR_N", 1, 250, "HighBias", "Normal",
			"NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPIE_TAU_F_P", 2, 53, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPIE_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPIE_THR_F_P", 2, 200, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPIE_THR_S_P", 7, 0, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPII_TAU_F_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPII_TAU_S_P", 2, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPII_THR_F_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_NPDPII_THR_S_P", 7, 40, "HighBias",
			"Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_PS_WEIGHT_EXC_F_N", 1, 90, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_PS_WEIGHT_EXC_S_N", 0, 255, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_PS_WEIGHT_INH_F_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_PS_WEIGHT_INH_S_N", 0, 100, "HighBias",
			"Normal", "NBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_PULSE_PWLK_P", 0, 43, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode, "C3_R2R_P", 4, 85, "HighBias", "Normal",
			"PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);

		caerLog(CAER_LOG_NOTICE, __func__, "init completed");

		state->init = true;
	}

	portable_clock_gettime_monotonic(&tend);
	double current_time = (double) (((double) tend.tv_sec + 1.0e-9 * tend.tv_nsec)
		- ((double) tstart.tv_sec + 1.0e-9 * tstart.tv_nsec));

	// let's change here the stimulation pattern
	if (current_time >= state->deltaT) {
		portable_clock_gettime_monotonic(&tstart);
		portable_clock_gettime_monotonic(&ttot);
		int tt = (int) ((sin((double) (6.2832 / state->period) * (ttot.tv_sec + 1.0e-9 * ttot.tv_nsec)) + 1.0) * 50);

		//updateCoarseFineBiasSetting(moduleData, "C0_IF_DC_P", 5, tt, "HighBias", "Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);
		//generatesBitsCoarseFineBiasSetting(state->eventSourceConfigNode,
		//	"C0_IF_DC_P", 5, tt, "HighBias", "Normal", "PBias", true, DYNAPSE_CONFIG_DYNAPSE_U0);

	}

}

static void caerReservoirConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	RSFilterState state = moduleData->moduleState;

	// this will update parameters, from user input
	state->deltaT = sshsNodeGetFloat(moduleData->moduleNode, "deltaT");
	state->period = sshsNodeGetFloat(moduleData->moduleNode, "period");
	state->ieratio = sshsNodeGetFloat(moduleData->moduleNode, "ieratio");

}

static void caerReservoirExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	RSFilterState state = moduleData->moduleState;
}

static void caerReservoirReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	RSFilterState state = moduleData->moduleState;
}

