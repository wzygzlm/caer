//netparser.c

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "modules/ini/dynapse_utils.h"  // useful constants
#include "Neuron.h"
#include <libcaer/devices/dynapse.h>

// Define basic 

static bool caerNetParserInit(caerModuleData moduleData);
static void caerNetParserExit(caerModuleData moduleData);
static void caerNetParserModuleConfig(caerModuleData moduleData);
void caerNetParserSetBiases(caerModuleData moduleData);
void caerClearConnections(caerModuleData);

//static void caerNetParserReset(caerModuleData moduleData);

struct NETPARSER_state {
	caerDeviceHandle eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	// user settings
	ConnectionManager manager;
	bool programTXT;
	bool programXML;
	bool bias;
	bool clear;
	int16_t sourceID;
};

typedef struct NETPARSER_state *NetParserState;

const static struct caer_module_functions caerNetParserFunctions = { .moduleConfigInit = NULL, .moduleInit =
	&caerNetParserInit, .moduleRun = NULL, .moduleConfig = &caerNetParserModuleConfig, .moduleExit = &caerNetParserExit,
	.moduleReset =
	NULL };

static const struct caer_event_stream_in caerNetParserInputs[] = {
	{ .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_module_info ModuleInfo = { .version = 1, .name = "netParser", .description =
	"Parse network connectivity files and configure a Dynap-SE.", .type = CAER_MODULE_OUTPUT, .memSize =
	sizeof(struct NETPARSER_state), .functions = &caerNetParserFunctions, .inputStreamsSize = 1, .inputStreams =
	caerNetParserInputs, .outputStreamsSize = 0, .outputStreams = NULL };

caerModuleInfo caerModuleGetInfo(void) {
	return (&ModuleInfo);
}

static bool caerNetParserInit(caerModuleData moduleData) {

	caerLog(CAER_LOG_DEBUG, __func__, "NET PARSER: INIT\n");

	NetParserState state = (NETPARSER_state*) moduleData->moduleState;
	// create parameters
	sshsNodeCreateBool(moduleData->moduleNode, "Program Network from .txt", false, SSHS_FLAGS_NORMAL, "def");
	sshsNodeCreateString(moduleData->moduleNode, "txt_file", "./modules/netparser/networks/hellonet.txt", 1, 4096,
		SSHS_FLAGS_NORMAL, "File to load network connnectivity from.");

	sshsNodeCreateBool(moduleData->moduleNode, "Program Network from .xml", false, SSHS_FLAGS_NORMAL, "def");
	sshsNodeCreateString(moduleData->moduleNode, "xml_file", "./modules/netparser/networks/hellonet.xml", 1, 4096,
		SSHS_FLAGS_NORMAL, "File to load network connnectivity from.");

	sshsNodeCreateBool(moduleData->moduleNode, "Set Default Spiking Biases", false, SSHS_FLAGS_NORMAL, "def");
	sshsNodeCreateBool(moduleData->moduleNode, "Clear Network\n(this will take about a minute)", false,
		SSHS_FLAGS_NORMAL, "def");

	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setSram", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setCam", false);

	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	state->sourceID = sourceID;
	state->eventSourceConfigNode = caerMainloopGetSourceNode(sourceID);
	state->eventSourceModuleState = (caerDeviceHandle) caerMainloopGetSourceState(U16T(state->sourceID));

	free(inputs);

	// Makes sure subsequent code is only run once dynapse is initiliazed
	sshsNode sourceInfo = caerMainloopGetSourceInfo(state->sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	state->programTXT = sshsNodeGetBool(moduleData->moduleNode, "Program Network from .txt");
	state->programXML = sshsNodeGetBool(moduleData->moduleNode, "Program Network from .xml");
	state->bias = sshsNodeGetBool(moduleData->moduleNode, "Set Default Spiking Biases");
	state->clear = sshsNodeGetBool(moduleData->moduleNode, "Clear Network\n(this will take about a minute)");

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	//Instantiate manager
	state->eventSourceModuleState = (caerDeviceHandle) caerMainloopGetSourceState(U16T(state->sourceID));
	state->manager = ConnectionManager(state->eventSourceModuleState);

	return (true);

}

void caerNetParserSetBiases(caerModuleData moduleData) {

	NetParserState state = (NETPARSER_state*) moduleData->moduleState;

	for (uint8_t chipId = 0; chipId < 4; chipId++) {

		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, U32T(chipId));

		for (uint8_t coreId = 0; coreId < 4; coreId++) {
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTAU_N", 7, 34, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTAU_N", 7, 35, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTHR_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTHR_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHW_P", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHW_P", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_BUF_P", 3, 79, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_BUF_P", 3, 80, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_CASC_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_CASC_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_DC_P", 5, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_DC_P", 5, 2, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_NMDA_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_NMDA_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_RFR_N", 2, 179, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_RFR_N", 2, 180, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU1_N", 4, 224, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU1_N", 4, 225, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU2_N", 4, 224, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU2_N", 4, 225, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_THR_N", 2, 179, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_THR_N", 2, 180, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_F_P", 6, 149, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_F_P", 6, 150, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_S_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_S_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_F_P", 0, 199, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_F_P", 0, 200, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_S_P", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_S_P", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_F_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_F_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_S_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_S_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_F_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_F_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_S_P", 7, 39, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_S_P", 7, 40, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 251, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 250, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PULSE_PWLK_P", 3, 49, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PULSE_PWLK_P", 3, 50, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "R2R_P", 4, 84, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "R2R_P", 4, 85, true);
		}
	}

	for (uint8_t chipId = 0; chipId < 4; chipId++) {

		caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, U32T(chipId));

		for (uint8_t coreId = 0; coreId < 4; coreId++) {
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTAU_N", 7, 35, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTHR_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHW_P", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_BUF_P", 3, 80, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_CASC_N", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_DC_P", 7, 1, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_NMDA_N", 7, 0, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_RFR_N", 0, 108, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU1_N", 6, 24, false);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU2_N", 5, 15, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_THR_N", 4, 20, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_F_P", 4, 36, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_S_P", 5, 38, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_F_P", 2, 200, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_S_P", 2, 200, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_F_P", 5, 41, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_S_P", 5, 41, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_F_P", 0, 150, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_S_P", 0, 150, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 114, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_S_N", 0, 100, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_F_N", 0, 100, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_S_N", 0, 114, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PULSE_PWLK_P", 0, 43, true);
			caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "R2R_P", 4, 85, true);
		}
	}
}

void caerClearConnections(caerModuleData moduleData) {

	NetParserState state = (NETPARSER_state*) moduleData->moduleState;

	caerLog(CAER_LOG_NOTICE, __func__, "Clearing SRAMs and CAMs...");

	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
	DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U0, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);

	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
	DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U1, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);

	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
	DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);

	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
	DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U3, 0);
	caerDeviceConfigSet(state->eventSourceModuleState, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);

	state->manager.Clear();

	caerLog(CAER_LOG_NOTICE, __func__, "Done Clearing Networks");

}

static void caerNetParserModuleConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);
	NetParserState state = (NETPARSER_state*) moduleData->moduleState;

	bool newProgramTXT = sshsNodeGetBool(moduleData->moduleNode, "Program Network from .txt");
	bool newProgramXML = sshsNodeGetBool(moduleData->moduleNode, "Program Network from .xml");

	bool newBiases = sshsNodeGetBool(moduleData->moduleNode, "Set Default Spiking Biases");
	bool newClearNetwork = sshsNodeGetBool(moduleData->moduleNode, "Clear Network\n(this will take about a minute)");

	bool operation_result = false;

	caerLog(CAER_LOG_DEBUG, __func__, "Running Config Module");

	if (newProgramTXT && !state->programTXT) {
		state->programTXT = true;

		caerLog(CAER_LOG_NOTICE, __func__, "Starting Board Connectivity Programming with txt file");
		std::string filePath = sshsNodeGetString(moduleData->moduleNode, "txt_file");
		//manager.Connect(new Neuron(2,2,2),new Neuron(2,2,6),1,1);
		operation_result = ReadNetTXT(&(state->manager), filePath);
		if (operation_result) {
			caerLog(CAER_LOG_NOTICE, __func__,
				("Succesfully Finished Board Connectivity Programming from " + filePath).c_str());
		}
		else {
			caerLog(CAER_LOG_ERROR, __func__,
				("Did NOT Finish Board Connectivity Programming from " + filePath).c_str());

		}

	}
	else if (!newProgramTXT && state->programTXT) {
		state->programTXT = false;
	}

	if (newProgramXML && !state->programTXT) {
		state->programTXT = true;

		caerLog(CAER_LOG_NOTICE, __func__, "Starting Board Connectivity Programming with xml file");
		std::string filePath = sshsNodeGetString(moduleData->moduleNode, "xml_file");

		//manager.Connect(new Neuron(2,2,2),new Neuron(2,2,6),1,1);
		operation_result = ReadNetXML(&(state->manager), filePath);

		if (operation_result) {
			caerLog(CAER_LOG_NOTICE, __func__,
				("Succesfully Finished Board Connectivity Programming from " + filePath).c_str());
		}
		else {
			caerLog(CAER_LOG_ERROR, __func__,
				("Did NOT Finish Board Connectivity Programming from " + filePath).c_str());

		}
	}
	else if (!newProgramXML && state->programTXT) {
		state->programTXT = false;
	}

	if (newBiases && !state->bias) {
		state->bias = true;
		caerLog(CAER_LOG_NOTICE, __func__, "Starting Bias setting");
		caerNetParserSetBiases(moduleData);
		caerLog(CAER_LOG_NOTICE, __func__, "Finished Bias setting");

	}
	else if (!newBiases && state->bias) {
		state->bias = false;
	}

	if (newClearNetwork && !state->clear) {
		state->clear = true;
		caerLog(CAER_LOG_NOTICE, __func__, "Starting Network Clearning");
		caerClearConnections(moduleData);
		caerLog(CAER_LOG_NOTICE, __func__, "Finished Network Clearning");

	}
	else if (!newBiases && state->clear) {
		state->clear = false;
	}

}

static void caerNetParserExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

}

