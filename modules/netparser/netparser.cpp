//netparser.c

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
//#include "modules/ini/dynapse_common.h"	// useful constants
#include "Neuron.h"

// Define basic 

static bool caerNetParserInit(caerModuleData moduleData);
static void caerNetParserExit(caerModuleData moduleData);
static void caerNetParserModuleConfig(caerModuleData moduleData);

//static void caerNetParserReset(caerModuleData moduleData);




struct NETPARSER_state {
	// user settings
	bool program;
	bool programmed;
	int16_t sourceID;

};

typedef struct NETPARSER_state *NetParserState;


const static struct caer_module_functions caerNetParserFunctions = { .moduleConfigInit = NULL, .moduleInit =
&caerNetParserInit, .moduleRun = NULL, .moduleConfig =
&caerNetParserModuleConfig, .moduleExit = &caerNetParserExit, .moduleReset =
NULL };

static const struct caer_event_stream_in caerNetParserInputs[] = {
	{ .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_module_info ModuleInfo = { .version = 1, .name = "netParser", .description = "Parse network connectivity files and configure a Dynap-SE.", .type =
CAER_MODULE_OUTPUT, .memSize = sizeof(struct NETPARSER_state), .functions = &caerNetParserFunctions,
		 .inputStreamsSize = 1, .inputStreams = caerNetParserInputs,
		 .outputStreamsSize = 0, .outputStreams = NULL };

caerModuleInfo caerModuleGetInfo(void) {
	return (&ModuleInfo);
}

static bool caerNetParserInit(caerModuleData moduleData) {
	NetParserState state = (NETPARSER_state*) moduleData->moduleState;
	// create parameters
	sshsNodeCreateBool(moduleData->moduleNode, "Program Network", false, SSHS_FLAGS_NORMAL, "def");
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setSram", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setCam", false);

	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	state -> sourceID = sourceID;

	free(inputs);

	caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - START1\n");

    sshsNodeCreateString(moduleData->moduleNode, "file", "/modules/netparser/helloNet.txt", 1, 4096, SSHS_FLAGS_NORMAL, "File to load network connnectivity from.");

    // Makes sure subsequent code is only run once dynapse is initiliazed
    sshsNode sourceInfo = caerMainloopGetSourceInfo(state->sourceID);
    if (sourceInfo == NULL) {
        return (false);
    }



	// update node state
	//state->loadBiases = sshsNodeGetBool(moduleData->moduleNode, "loadBiases");
	//state->setSram = sshsNodeGetBool(moduleData->moduleNode, "setSram");
	//state->setCam = sshsNodeGetBool(moduleData->moduleNode, "setCam");
    state->program = sshsNodeGetBool(moduleData->moduleNode, "Program Network");
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);
    
	// Nothing that can fail here.
	caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - DONE");
	return (true);



}

static void caerNetParserModuleConfig(caerModuleData moduleData) {
		caerModuleConfigUpdateReset(moduleData);
		NetParserState state = (NETPARSER_state*) moduleData->moduleState;

		bool newProgram = sshsNodeGetBool(moduleData->moduleNode, "Program Network");

		caerLog(CAER_LOG_NOTICE, __func__, "Requesting Board Connectivity Programming");   	
    			
		if (newProgram && !state->program) {
				state->program = true;
			    void *dynapseState = caerMainloopGetSourceState(state->sourceID);

    			caerDeviceHandle handle = *((caerDeviceHandle *) dynapseState);
			    caerLog(CAER_LOG_NOTICE, __func__, "Starting Board Connectivity Programming");
    			std::string filePath = sshsNodeGetString(moduleData->moduleNode, "file");
				ConnectionManager manager(handle);
				//manager.Connect(new Neuron(2,2,2),new Neuron(2,2,6),1,1);
    			ReadNet(manager, filePath);
    			caerLog(CAER_LOG_NOTICE, __func__, "Finished Board Connectivity Programming");

		}
		else if (!newProgram && state->program) {
				state->program = false;
		}

}

static void caerNetParserExit(caerModuleData moduleData) {

}
