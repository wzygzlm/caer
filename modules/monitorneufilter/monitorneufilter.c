/*
 *
 *  Created on: Dec, 2016
 *      Author: federico.corradi@inilabs.com
 */

#include <time.h>
#include <libcaer/devices/dynapse.h>
#include "modules/ini/dynapse_common.h"
#include <libcaer/events/spike.h>

struct MNFilter_state {
	caerInputDynapseState eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	int dynapse_u0_c0;
	int dynapse_u0_c1;
	int dynapse_u0_c2;
	int dynapse_u0_c3;	//chip id core id
	int dynapse_u1_c0;
	int dynapse_u1_c1;
	int dynapse_u1_c2;
	int dynapse_u1_c3;
	int dynapse_u2_c0;
	int dynapse_u2_c1;
	int dynapse_u2_c2;
	int dynapse_u2_c3;
	int dynapse_u3_c0;
	int dynapse_u3_c1;
	int dynapse_u3_c2;
	int dynapse_u3_c3;
	int16_t sourceID;
};

typedef struct MNFilter_state *MNFilterState;

static bool caerMonitorNeuFilterInit(caerModuleData moduleData);
static void caerMonitorNeuFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerMonitorNeuFilterExit(caerModuleData moduleData);
static void caerMonitorNeuFilterReset(caerModuleData moduleData, int16_t resetCallSourceID);

static struct caer_module_functions caerMonitorNeuFilterFunctions = { .moduleInit =
	&caerMonitorNeuFilterInit, .moduleRun = &caerMonitorNeuFilterRun, .moduleExit = &caerMonitorNeuFilterExit, .moduleReset =
	&caerMonitorNeuFilterReset };

static const struct caer_event_stream_in moduleInputs[] = {
    { .type = SPIKE_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "MonitorNeuronFilter",
	.description = "Select neurons to monitor",
	.type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct MNFilter_state),
	.functions = &caerMonitorNeuFilterFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = NULL,
	.outputStreamsSize = NULL
};

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}


static bool caerMonitorNeuFilterInit(caerModuleData moduleData) {
	MNFilterState state = moduleData->moduleState;

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	state->sourceID = inputs[0];
	free(inputs);

	state->eventSourceModuleState = caerMainloopGetSourceState(U16T(state->sourceID));
	state->eventSourceConfigNode = caerMainloopGetSourceNode(U16T(state->sourceID));

	// defaults is first neurons of all cores
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u0_c0", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u0_c1", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u0_c2", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u0_c3", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");

	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u1_c0", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u1_c1", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u1_c2", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u1_c3", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");

	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u2_c0", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u2_c1", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u2_c2", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u2_c3", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");

	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u3_c0", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u3_c1", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u3_c2", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");
	sshsNodeCreateInt(moduleData->moduleNode, "dynapse_u3_c3", 0, 0, 255, SSHS_FLAGS_NORMAL, "Neuron id");

	// variables
	state->dynapse_u0_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0");
	state->dynapse_u0_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1");
	state->dynapse_u0_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2");
	state->dynapse_u0_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3");

	state->dynapse_u1_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0");
	state->dynapse_u1_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1");
	state->dynapse_u1_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2");
	state->dynapse_u1_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3");

	state->dynapse_u2_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0");
	state->dynapse_u2_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1");
	state->dynapse_u2_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2");
	state->dynapse_u2_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3");

	state->dynapse_u3_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0");
	state->dynapse_u3_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1");
	state->dynapse_u3_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2");
	state->dynapse_u3_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3");

	// Nothing that can fail here.
	return (true);
}

static void caerMonitorNeuFilterRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {

	caerSpikeEventPacketConst spike =
		(caerSpikeEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, SPIKE_EVENT);


	MNFilterState state = moduleData->moduleState;

	caerInputDynapseState stateSource = state->eventSourceModuleState;

	// if changed we set it
	if(state->dynapse_u0_c0 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 0, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u0_c0 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0"));
			state->dynapse_u0_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c0");
		}
	}
	if(state->dynapse_u0_c1 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 1, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u0_c1 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1"));
			state->dynapse_u0_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c1");
		}
	}
	if(state->dynapse_u0_c2 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 2, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u0_c2 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2"));
			state->dynapse_u0_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c2");
		}
	}
	if(state->dynapse_u0_c3 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 3, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u0_c3 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3"));
			state->dynapse_u0_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u0_c3");
		}
	}

	if(state->dynapse_u1_c0 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 0, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u1_c0 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0"));
			state->dynapse_u1_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c0");
		}

	}
	if(state->dynapse_u1_c1 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 1, (uint32_t)  sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u1_c1 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1"));
			state->dynapse_u1_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c1");
		}
	}
	if(state->dynapse_u1_c2 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 2, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u1_c2 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2"));
			state->dynapse_u1_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c2");
		}
	}
	if(state->dynapse_u1_c3 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 3, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u1_c3 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3"));
			state->dynapse_u1_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u1_c3");
		}
	}

	if(state->dynapse_u2_c0 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 0, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u2_c0 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0"));
			state->dynapse_u2_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c0");
		}
	}
	if(state->dynapse_u2_c1 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 1, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u2_c1 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1"));
			state->dynapse_u2_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c1");
		}
	}
	if(state->dynapse_u2_c2 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 2, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u2_c2 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2"));
			state->dynapse_u2_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c2");
		}
	}
	if(state->dynapse_u2_c3 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 3, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u2_c3 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3"));
			state->dynapse_u2_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u2_c3");
		}
	}

	if(state->dynapse_u3_c0 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 0, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u3_c0 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0"));
			state->dynapse_u3_c0 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c0");
		}
	}
	if(state->dynapse_u3_c1 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 1, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u3_c1 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1"));
			state->dynapse_u3_c1 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c1");
		}
	}
	if(state->dynapse_u3_c2 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 2, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u3_c2 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2"));
			state->dynapse_u3_c2 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c2");
		}
	}
	if(state->dynapse_u3_c3 != sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3")){
		if(sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3") < 0 || sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3") > 255){
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Wrong neuron ID %d, please choose a value from [0,255]", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3"));
		}else{
			caerDeviceConfigSet(stateSource->deviceState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
			caerDeviceConfigSet(stateSource->deviceState,
					DYNAPSE_CONFIG_MONITOR_NEU, 3, (uint32_t) sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3"));
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString, "Monitoring neuron dynapse_u3_c3 num: %d", sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3"));
			state->dynapse_u3_c3 = sshsNodeGetInt(moduleData->moduleNode, "dynapse_u3_c3");
		}
	}

}

static void caerMonitorNeuFilterExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.

}

static void caerMonitorNeuFilterReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

}


