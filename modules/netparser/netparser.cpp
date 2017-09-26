//netparser.c

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "modules/ini/dynapse_utils.h"	// useful constants
#include "Neuron.h"

// Define basic 

static bool caerNetParserInit(caerModuleData moduleData);
static void caerNetParserExit(caerModuleData moduleData);
static void caerNetParserModuleConfig(caerModuleData moduleData);

//static void caerNetParserReset(caerModuleData moduleData);


struct NETPARSER_state {
	sshsNode eventSourceConfigNode;
	// user settings
    ConnectionManager manager;
	bool programTXT;
    bool programXML;
	bool bias;
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
	sshsNodeCreateBool(moduleData->moduleNode, "Import from .txt", false, SSHS_FLAGS_NORMAL, "def");
    sshsNodeCreateBool(moduleData->moduleNode, "Import from .xml", false, SSHS_FLAGS_NORMAL, "def");
    sshsNodeCreateBool(moduleData->moduleNode, "Set Biases", false, SSHS_FLAGS_NORMAL, "def");
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setSram", false);
	//sshsNodePutBoolIfAbsent(moduleData->moduleNode, "setCam", false);

	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	state->sourceID = sourceID;
	state->eventSourceConfigNode = caerMainloopGetSourceNode(sourceID);


	free(inputs);

	caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - START1\n");

    sshsNodeCreateString(moduleData->moduleNode, "txt_file", "/modules/netparser/n1.txt", 1, 4096, SSHS_FLAGS_NORMAL, "File to load network connnectivity from.");
    sshsNodeCreateString(moduleData->moduleNode, "xml_file", "/modules/netparser/output2.xml", 1, 4096, SSHS_FLAGS_NORMAL, "File to load network connnectivity from.");

    // Makes sure subsequent code is only run once dynapse is initiliazed
    sshsNode sourceInfo = caerMainloopGetSourceInfo(state->sourceID);
    if (sourceInfo == NULL) {
        return (false);
    }

    //caerNetParserSetBiases(moduleData)

	// update node state
	//state->loadBiases = sshsNodeGetBool(moduleData->moduleNode, "loadBiases");
	//state->setSram = sshsNodeGetBool(moduleData->moduleNode, "setSram");
	//state->setCam = sshsNodeGetBool(moduleData->moduleNode, "setCam");
    state->programTXT = sshsNodeGetBool(moduleData->moduleNode, "ZZZfrom .txt");
    state->programXML = sshsNodeGetBool(moduleData->moduleNode, "ZZZfrom .xml");
    state->bias = sshsNodeGetBool(moduleData->moduleNode, "Set Biases");
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);
    
	// Nothing that can fail here.
	caerLog(CAER_LOG_NOTICE, __func__, "NET PARSER: INIT - DONE");
	return (true);

    //Instantiate manager
    void *dynapseState = caerMainloopGetSourceState(state->sourceID);
    caerDeviceHandle handle = *((caerDeviceHandle *) dynapseState);
    state->manager = ConnectionManager(handle);


}


void caerNetParserSetBiases(caerModuleData moduleData){ 
 
  NetParserState state = (NETPARSER_state*) moduleData->moduleState; 

  	caerLog(CAER_LOG_NOTICE, __func__, "Biasing U0");
 
    for(size_t coreid=0; coreid<4; coreid++){ 
      //caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHTAU_N", 7, 34, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHTAU_N",7, 35, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHTHR_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_AHW_P",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_BUF_P",3, 80, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_CASC_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_DC_P",7, 1, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_NMDA_N",7, 0, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_RFR_N",0, 108, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_TAU1_N",6, 24, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_TAU2_N",5, 15, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "IF_THR_N",4, 20, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_TAU_F_P",4, 36, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_TAU_S_P",5, 38,true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_THR_F_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPIE_THR_S_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_TAU_F_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_TAU_S_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_THR_F_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "NPDPII_THR_S_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_EXC_F_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_EXC_S_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_INH_F_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PS_WEIGHT_INH_S_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "PULSE_PWLK_P", 0, 43, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U0, coreid, "R2R_P", 4, 85, true); 
    } 
 	caerLog(CAER_LOG_NOTICE, __func__, "Biasing U1");
    for(size_t coreid=0; coreid<4; coreid++){ 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_AHTAU_N",7, 35, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_AHTHR_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_AHW_P",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_BUF_P",3, 80, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_CASC_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_DC_P",7, 1, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_NMDA_N",7, 0, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_RFR_N",0, 108, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_TAU1_N",6, 24, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_TAU2_N",5, 15, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "IF_THR_N",4, 20, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPIE_TAU_F_P",4, 36, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPIE_TAU_S_P",5, 38,true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPIE_THR_F_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPIE_THR_S_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPII_TAU_F_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPII_TAU_S_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPII_THR_F_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "NPDPII_THR_S_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "PS_WEIGHT_EXC_F_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "PS_WEIGHT_EXC_S_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "PS_WEIGHT_INH_F_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "PS_WEIGHT_INH_S_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "PULSE_PWLK_P", 0, 43, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U1, coreid, "R2R_P", 4, 85, true);
    } 
 	caerLog(CAER_LOG_NOTICE, __func__, "Biasing U2");
    for(size_t coreid=0; coreid<4; coreid++){ 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_AHTAU_N",7, 35, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_AHTHR_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_AHW_P",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_BUF_P",3, 80, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_CASC_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_DC_P",7, 1, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_NMDA_N",7, 0, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_RFR_N",0, 108, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_TAU1_N",6, 24, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_TAU2_N",5, 15, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "IF_THR_N",4, 20, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPIE_TAU_F_P",4, 36, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPIE_TAU_S_P",5, 38,true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPIE_THR_F_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPIE_THR_S_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPII_TAU_F_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPII_TAU_S_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPII_THR_F_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "NPDPII_THR_S_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "PS_WEIGHT_EXC_F_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "PS_WEIGHT_EXC_S_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "PS_WEIGHT_INH_F_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "PS_WEIGHT_INH_S_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "PULSE_PWLK_P", 0, 43, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U2, coreid, "R2R_P", 4, 85, true);
    } 

 	caerLog(CAER_LOG_NOTICE, __func__, "Biasing U3");
    for(size_t coreid=0; coreid<4; coreid++){ 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_AHTAU_N",7, 35, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_AHTHR_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_AHW_P",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_BUF_P",3, 80, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_CASC_N",7, 1, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_DC_P",7, 1, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_NMDA_N",7, 0, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_RFR_N",0, 108, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_TAU1_N",6, 24, false); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_TAU2_N",5, 15, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "IF_THR_N",4, 20, true);  
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPIE_TAU_F_P",4, 36, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPIE_TAU_S_P",5, 38,true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPIE_THR_F_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPIE_THR_S_P",2, 200, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPII_TAU_F_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPII_TAU_S_P",5, 41, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPII_THR_F_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "NPDPII_THR_S_P",0, 150, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "PS_WEIGHT_EXC_F_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "PS_WEIGHT_EXC_S_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "PS_WEIGHT_INH_F_N",0, 100, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "PS_WEIGHT_INH_S_N",0, 114, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "PULSE_PWLK_P", 0, 43, true); 
        caerDynapseSetBiasCore(state->eventSourceConfigNode, DYNAPSE_CONFIG_DYNAPSE_U3, coreid, "R2R_P", 4, 85, true);
    } 
}

static void caerNetParserModuleConfig(caerModuleData moduleData) {
		caerModuleConfigUpdateReset(moduleData);
		NetParserState state = (NETPARSER_state*) moduleData->moduleState;

		bool newProgramTXT = sshsNodeGetBool(moduleData->moduleNode, "ZZZfrom .txt");
        bool newProgramXML = sshsNodeGetBool(moduleData->moduleNode, "ZZZfrom .xml");

		bool newBiases = sshsNodeGetBool(moduleData->moduleNode, "Set Biases");

		caerLog(CAER_LOG_NOTICE, __func__, "Running Config Module");   	
    			
		if (newProgramTXT && !state->programTXT) {
				state->programTXT = true;
			    
			    caerLog(CAER_LOG_NOTICE, __func__, "Starting Board Connectivity Programming with txt file");
    			std::string filePath = sshsNodeGetString(moduleData->moduleNode, "txt_file");
				//manager.Connect(new Neuron(2,2,2),new Neuron(2,2,6),1,1);
    			ReadNet(state->manager, filePath);
    			caerLog(CAER_LOG_NOTICE, __func__, "Finished Board Connectivity Programming with txt file");

		}
		else if (!newProgramTXT && state->programTXT) {
				state->programTXT = false;
		}

        if (newProgramXML && !state->programTXT) {
                state->programTXT = true;
                
                caerLog(CAER_LOG_NOTICE, __func__, "Starting Board Connectivity Programming with xml file");
                std::string filePath = sshsNodeGetString(moduleData->moduleNode, "xml_file");

                //manager.Connect(new Neuron(2,2,2),new Neuron(2,2,6),1,1);
                ReadXMLNet(state->manager, filePath);
                caerLog(CAER_LOG_NOTICE, __func__, "Finished Board Connectivity Programming with xml file");

        }
        else if (!newProgramXML && state->programTXT) {
                state->programTXT = false;
        }

		if (newBiases && !state->bias) {
				state->bias = true;
			    void *dynapseState = caerMainloopGetSourceState(state->sourceID);

    			caerDeviceHandle handle = *((caerDeviceHandle *) dynapseState);
			    caerLog(CAER_LOG_NOTICE, __func__, "Starting Bias setting");
    			caerNetParserSetBiases(moduleData);
    			caerLog(CAER_LOG_NOTICE, __func__, "Finished Bias setting");

		}
		else if (!newBiases && state->bias) {
				state->bias = false;
		}

}

static void caerNetParserExit(caerModuleData moduleData) {

}


 
