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
#include "modules/ini/dynapse_utils.h"

struct RSFilter_state {
	caerDeviceHandle eventSourceModuleState;
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

static bool caerReservoirInit(caerModuleData moduleData);
static void caerReservoirRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerReservoirConfig(caerModuleData moduleData);
static void caerReservoirExit(caerModuleData moduleData);
static void caerReservoirReset(caerModuleData moduleData, int16_t resetCallSourceID);

static struct caer_module_functions caerReservoirFunctions = { .moduleInit = &caerReservoirInit, .moduleRun =
	&caerReservoirRun, .moduleConfig = &caerReservoirConfig, .moduleExit = &caerReservoirExit, .moduleReset =
	&caerReservoirReset };

static const struct caer_event_stream_in moduleInputs[] = { { .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "Reservoir", .description =
	"Reservoir of neurons", .type = CAER_MODULE_OUTPUT, .memSize = sizeof(struct RSFilter_state), .functions =
	&caerReservoirFunctions, .inputStreams = moduleInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL, .outputStreamsSize = 0 };

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
	sshsNodeCreateFloat(moduleData->moduleNode, "ieratio", 5.0f, 0.0f, 10.0f, SSHS_FLAGS_NORMAL, "Excitatory to inhibitory connectivity ratio");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodeCreateShort(sourceInfoNode, "dataSizeX", DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX, DYNAPSE_X4BOARD_NEUX * 16, SSHS_FLAGS_NORMAL, "number of neurons in X");
		sshsNodeCreateShort(sourceInfoNode, "dataSizeY", DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY, DYNAPSE_X4BOARD_NEUY * 16, SSHS_FLAGS_NORMAL, "number of neurons in Y");
	}

	// internals
	state->init = false;
	state->setbias = false;

	// Add config listeners last - let's the user interact with the parameter -
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerReservoirRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	caerSpikeEventPacketConst spike = (caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in,
		SPIKE_EVENT);

	RSFilterState state = moduleData->moduleState;

	if(spike == NULL){
		return;
	}

	// now we can do crazy processing etc..
	// first find out which one is the module producing the spikes. and get usb handle
	// --- start  usb handle / from spike event source id
	int sourceID = caerEventPacketHeaderGetEventSource(&spike->packetHeader);
	state->eventSourceModuleState = caerMainloopGetSourceState(U16T(sourceID));
	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(sourceID));
	if (state->eventSourceModuleState == NULL || state->eventSourceConfigNode == NULL) {
		return;
	}

	// --- end usb handle

	if (state->init == false) {
		// do the initialization
		caerLog(CAER_LOG_NOTICE, __func__, "Initialization of the Reservoir Network");

		// load silent biases while configuring, to speed up configuration
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, 0, "IF_DC_P", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, 0, "IF_DC_P", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, 0, "IF_DC_P", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, 0, "IF_DC_P", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, 0, "IF_THR_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, 0, "IF_THR_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, 0, "IF_THR_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, 0, "IF_THR_N", 7, 0, true);

		// Select chip to operate on
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);

		// Clear all cam for that particular chip
		caerLog(CAER_LOG_NOTICE, __func__, "Started clearing CAM ");
		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CLEAR_CAM, DYNAPSE_CONFIG_DYNAPSE_U0, 0);
		caerLog(CAER_LOG_NOTICE, __func__, "CAM cleared");

		// program connections for input stimulus
		// input will go only on the first 256 neurons
		uint32_t neuronId;
		int neuronToStim = 256;
		uint16_t preAddress = 1;
		caerLog(CAER_LOG_NOTICE, __func__, "Started programming cam for input stimulus.. one every two neurons in the first core");
		for (neuronId = 0; neuronId < neuronToStim; neuronId = neuronId+2) {
			caerDynapseWriteCam(state->eventSourceModuleState, preAddress, neuronId, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
		}
		caerLog(CAER_LOG_NOTICE, __func__, "CAM programmed successfully.");

		// load biases
		for (uint8_t coreId = 0; coreId < 4; coreId++) {
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_AHTAU_N", 7, 34, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_AHTAU_N", 7, 35, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_AHTHR_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_AHTHR_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_AHW_P", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_AHW_P", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_BUF_P", 3, 79, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_BUF_P", 3, 80, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_CASC_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_CASC_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_DC_P", 5, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_DC_P", 5, 2, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_NMDA_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_NMDA_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_RFR_N", 2, 179, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_RFR_N", 2, 180, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_TAU1_N", 4, 224, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_TAU1_N", 4, 225, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_TAU2_N", 4, 224, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_TAU2_N", 4, 225, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_THR_N", 2, 179, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "IF_THR_N", 2, 200, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_TAU_F_P", 6, 149, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_TAU_F_P", 6, 150, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_TAU_S_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_TAU_S_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_THR_F_P", 0, 199, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_THR_F_P", 0, 200, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_THR_S_P", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPIE_THR_S_P", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_TAU_F_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_TAU_F_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_TAU_S_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_TAU_S_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_THR_F_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_THR_F_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_THR_S_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "NPDPII_THR_S_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_EXC_F_N", 3, 51, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_EXC_F_N", 3, 50, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_EXC_S_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_EXC_S_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_INH_F_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_INH_F_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_INH_S_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PS_WEIGHT_INH_S_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PULSE_PWLK_P", 3, 49, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "PULSE_PWLK_P", 3, 50, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "R2R_P", 4, 84, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreId, "R2R_P", 4, 85, true);
		}
		caerLog(CAER_LOG_NOTICE, __func__, "init completed");
		state->init = true;
	}

}

static void caerReservoirConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	RSFilterState state = moduleData->moduleState;

	// this will update parameters, from user input
	state->ieratio = sshsNodeGetFloat(moduleData->moduleNode, "ieratio");

}

static void caerReservoirExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	RSFilterState state = moduleData->moduleState;
}

static void caerReservoirReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	RSFilterState state = moduleData->moduleState;
}

