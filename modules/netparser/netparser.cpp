//netparser.c

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
//#include "modules/ini/dynapse_common.h"	// useful constants
#include "Neuron.h"

// Define basic 

static bool caerNetParserInit(caerModuleData moduleData);
static void caerNetParserExit(caerModuleData moduleData);
//static void caerNetParserReset(caerModuleData moduleData);


const static struct caer_module_functions caerNetParserFunctions = { .moduleConfigInit = NULL, .moduleInit =
&caerNetParserInit, .moduleRun = NULL, .moduleConfig =
NULL, .moduleExit = &caerNetParserExit, .moduleReset =
NULL };

static const struct caer_event_stream_in caerNetParserInputs[] = {
	{ .type = SPIKE_EVENT, .number = 1, .readOnly = true } };

static const struct caer_module_info NetParserInfo = { .version = 1, .name = "netParser", .description = "Parse network connectivity files and configure a Dynap-SE.", .type =
CAER_MODULE_OUTPUT, .memSize = 0, .functions = &caerNetParserFunctions,
		 .inputStreamsSize = 1, .inputStreams = caerNetParserInputs,
		 .outputStreamsSize = 0, .outputStreams = NULL };

caerModuleInfo caerModuleGetInfo(void) {
	return (&NetParserInfo);
}

static bool caerNetParserInit(caerModuleData moduleData) {
	// create parameters
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "loadBiases", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setSram", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setCam", false);

	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - START1\n");

    sshsNodeCreateString(moduleData->moduleNode, "file", "/modules/netparser/helloNet.txt", 1, 4096, SSHS_FLAGS_NORMAL, "File to load network connnectivity from.");

    // Makes sure subsequent code is only run once dynapse is initiliazed
    sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
    if (sourceInfo == NULL) {
        return (false);
    }

    void *dynapseState = caerMainloopGetSourceState(sourceID);

    caerDeviceHandle handle = *((caerDeviceHandle *) dynapseState);

	// update node state
	//state->loadBiases = sshsNodeGetBool(moduleData->moduleNode, "loadBiases");
	//state->setSram = sshsNodeGetBool(moduleData->moduleNode, "setSram");
	//state->setCam = sshsNodeGetBool(moduleData->moduleNode, "setCam");

    caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - LOAD");
    std::string filePath = sshsNodeGetString(moduleData->moduleNode, "file");
	ConnectionManager manager(handle);
	//manager.Connect(new Neuron(2,2,2),new Neuron(2,2,6),1,1);
    ReadNet(manager, filePath);

	// Nothing that can fail here.
	caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - DONE");
	return (true);



}

static void caerNetParserExit(caerModuleData moduleData) {

}
