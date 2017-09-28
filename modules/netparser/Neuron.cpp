//
// Created by rodrigo on 5/30/17.
//


//
// Created by rodrigo on 5/30/17.
//


#include "Neuron.h"
using namespace std;




Neuron::Neuron(uint8_t chip_n ,uint8_t core_n ,uint8_t neuron_n):
        chip(chip_n),
        core(core_n),
        neuron(neuron_n)
{
    SRAM = vector<Neuron *>(0);
    CAM = vector<Neuron *>(0);
    synapse_type = vector<u_int8_t>(0);
}

Neuron::Neuron():
        chip(0),
        core(0),
        neuron(0)
{
    SRAM = vector<Neuron *>(0);
    CAM = vector<Neuron *>(0);
    synapse_type = vector<u_int8_t>(0);
}

string Neuron::GetLocString()const{
    stringstream ss;
    ss << 'U' << setw(2) << setfill('0') << unsigned(chip) << '-'
       << 'C' << setw(2) << setfill('0') << unsigned(core) << '-'
       << 'N' << setw(3) << setfill('0') << unsigned(neuron);
    return ss.str();
}

void Neuron::Print() const {
    cout << GetLocString() << endl ;
}

void Neuron::PrintSRAM() {
    if (this->SRAM.size() > 0) {
        for (vector<Neuron *>::iterator i = this->SRAM.begin(); i != this->SRAM.end(); ++i) {
            (*i)->Print();
        }
    }else{
        cout << "empty SRAM" << endl;
    }
}


string Neuron::GetSRAMString() {
    stringstream ss;

    if (this->SRAM.size() > 0) {
        for (vector<Neuron *>::iterator i = this->SRAM.begin(); i != this->SRAM.end(); ++i) {
            ss << (*i)->GetLocString() << " ";
        }
    }else{
        ss << "empty SRAM";
    }
    return ss.str();
}

void Neuron::PrintCAM() {
    if (this->CAM.size() > 0) {
        for (vector<Neuron *>::iterator i = this->CAM.begin(); i != this->CAM.end(); ++i){
            (*i)->Print();
        }
    }else{
        cout << "empty CAM" <<endl;
    }
}

string Neuron::GetCAMString() {
    stringstream ss;

    if (this->CAM.size() > 0) {
        for (vector<Neuron *>::iterator i = this->CAM.begin(); i != this->CAM.end(); ++i) {
            ss << (*i)->GetLocString() << " ";
        }
    }else{
        ss << "empty CAM";
    }
    return ss.str();
}

vector<Neuron *>::iterator Neuron::FindCamClash(Neuron * n){
    CamClashPred pred(n);
    return find_if(this->CAM.begin(), this->CAM.end(),pred);
}


CamClashPred::CamClashPred(Neuron* neuronA_) : neuronA_(neuronA_){}

bool CamClashPred::operator()(const Neuron* neuronB){
    return (neuronA_->neuron == neuronB->neuron)&&(neuronA_->core == neuronB->core);
}


// Make neuron object comparable
bool operator < (const Neuron& x, const Neuron& y) {
    return tie(x.chip, x.core, x.neuron) < tie(y.chip, y.core, y.neuron);
}

bool operator > (const Neuron& x, const Neuron& y) {
    return tie(x.chip, x.core, x.neuron) > tie(y.chip, y.core, y.neuron);
}

bool operator == (const Neuron& x, const Neuron& y) {
    return tie(x.chip, x.core, x.neuron) == tie(y.chip, y.core, y.neuron);
}

vector<uint8_t> ConnectionManager::CalculateBits(int chip_from, int chip_to){
    // TODO: Make programatic

    // We can also calculate programatically:
    // Program SRAM: {(South/North, steps x, West/East, steps y}
    // Direction: Assign 0->10, 1->00, 2->11, 3->01 and subtract with overflow
    // Ex: 3 - 1 = 01 - 10 = 01
    // Steps: Assign 0->00, 1->10, 2->01, 3->11 and add with overflow:
    // Ex: 3 + 1 = 11 + 10 = 01
    // Results, bit for 3->1 = d0 s0 d1 s1 = 0 0 1 1

    vector<uint8_t> bits;
    if (chip_from == 0){
        if (chip_to == 0){return bits = {0,0,0,0};}
        else if (chip_to == 1){return bits = {0,1,0,0};}
        else if (chip_to == 2){return bits = {0,0,1,1};}
        else if (chip_to == 3){return bits = {0,1,1,1};}
    }
    else if (chip_from == 1){
        if (chip_to == 0){return bits = {1,1,0,0};}
        else if (chip_to == 1){return bits = {0,0,0,0};}
        else if (chip_to == 2){return bits = {1,1,1,1};}
        else if (chip_to == 3){return bits = {0,0,1,1};}
    }
    else if (chip_from == 2){
        if (chip_to == 0){return bits = {0,0,0,1};}
        else if (chip_to == 1){return bits = {0,1,0,1};}
        else if (chip_to == 2){return bits = {0,0,0,0};}
        else if (chip_to == 3){return bits = {0,1,0,0};}
    }
    else if (chip_from == 3){
        if (chip_to == 0){return bits = {1,1,0,1};}
        else if (chip_to == 1){return bits = {0,0,0,1};}
        else if (chip_to == 2){return bits = {1,1,0,0};}
        else if (chip_to == 3){return bits = {0,0,0,0};}
    }
}

// For SRAM: Hot coded
uint16_t ConnectionManager::GetDestinationCore(int core){
    // TODO: Add ability to send to multiple cores
    if(core == 0){
        return 1;
    }
    else if(core == 1){
        return 2;
    }
    else if(core == 2){
        return 4;
    }
    else if(core == 3){
        return 8;
    }
    else{
        return 0;
    }
}

// For CAM
uint32_t ConnectionManager::NeuronCamAddress(int neuron, int core){
    return (uint32_t) neuron + core*256;
}


// The Connection manager keeps track of the SRAM and CAM registers of all neurons
// involved in a connection (sparse). Since there is no real way to access the
// registers themselves in order for this to work you must piping all connection settings through
// pipe all your connection settings through this manager (don't call caerDynapseWriteSram/Cam directly)
void ConnectionManager::MakeConnection( Neuron * pre, Neuron * post, uint8_t cam_slots_number, uint8_t connection_type ){

    // In internal map
    pre->SRAM.push_back(post);


    vector<uint8_t> dirBits = CalculateBits(pre->chip, post->chip);

    // print SRAM command
    string message = "SRAM Settings: "+
    to_string(pre->chip)+ "  (" + 
    to_string(pre->core)+ ", " + 
    to_string(pre->neuron)+ ", " +   
    to_string(pre->core)+ ", " +
    to_string((bool)dirBits[0])+ ", " +
    to_string(dirBits[1])+ ", " +
    to_string((bool)dirBits[2])+ ", " +
    to_string(dirBits[3])+ ", " +
    to_string(pre->SRAM.size())+ ", " +
    string(to_string(GetDestinationCore(post->core)))+ ") ";

    caerLog(CAER_LOG_DEBUG, __func__, message.c_str());


    // Program SRAM
    caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, pre->chip);

    caerDynapseWriteSram(handle, pre->core, pre->neuron, pre->core, (bool)dirBits[0],
                         dirBits[1], (bool)dirBits[2], dirBits[3], (uint16_t) pre->SRAM.size()+1, //first SRAM is for debbugging
                         GetDestinationCore(post->core));


    message = "CAM Settings: "+ 
    to_string(post->chip)+ ", " +
    to_string(cam_slots_number)+ " (" +
    to_string(NeuronCamAddress(pre->neuron,pre->core))+ ", " +
    to_string(NeuronCamAddress(post->neuron,post->core))+ ", " +
    to_string(post->CAM.size())+ ", " +
    to_string(connection_type)+ ") ";

    caerLog(CAER_LOG_DEBUG, __func__, message.c_str());

    // Program CAM
    caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, post->chip);

    // For each cam in cam_slot_num
    int curr_cam_size = post->CAM.size();
    for (int n=post->CAM.size(); n < curr_cam_size + cam_slots_number; n++) {
        post->CAM.push_back(pre);
        caerDynapseWriteCam(handle, NeuronCamAddress(pre->neuron, pre->core), NeuronCamAddress(post->neuron,post->core),
                        (uint32_t) n, connection_type);
    }
    


}

bool ConnectionManager::CheckAndConnect(Neuron * pre, Neuron * post, uint8_t cam_slots_number, uint8_t connection_type ){
    string message = string("Attempting to connect " + pre->GetLocString() + "-" + to_string(connection_type)
            + "-" + to_string(cam_slots_number) + "->" + post->GetLocString());
    caerLog(CAER_LOG_DEBUG, __func__, message.c_str());

    if(!(*pre == *post)) {
        if(connection_type >= 0 & connection_type < 4)
            if (pre->SRAM.size() < 3) {
                if (post->CAM.size() > 0) {
                    if (64 - post->CAM.size() >= cam_slots_number) {

                        //find instances where contents in the cam will clash with the new element being added
                        auto it = post->FindCamClash(pre);

                        //if no clashes, connect
                        if (it == post->CAM.end()) {
                            caerLog(CAER_LOG_DEBUG, __func__, "Passed tests");
                            MakeConnection(pre, post, cam_slots_number, connection_type);
                            return true;

                        } else {
                            message = string("CAM Clash at " + post->GetLocString() + " between " + (*it)->GetLocString() + " and " + pre->GetLocString());
                            caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
                            //throw message;
                            
                        }

                    } else {
                        message = "CAM Overflow for " + post->GetLocString() + ".\nCAM slot number requested (" + to_string(cam_slots_number)+ 
                        ") exceeds number of cam slot left (" + to_string(64 - post->CAM.size()) + ")";
                        caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
                        //throw "CAM Size Limit (64) Reached: " + post->GetLocString();
                    }
                } else {
                    //If CAM is empty, connect
                    caerLog(CAER_LOG_DEBUG, __func__, "Passed tests");
                    MakeConnection(pre, post, cam_slots_number, connection_type);
                    return true;
                }
            } else {
                message = "SRAM Size Limit (3) Reached: " + pre->GetLocString();
                caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
                //throw "SRAM Size Limit (4) Reached: " + pre->GetLocString();
            }
        else {
            message = "Invalid Connection Type: " + connection_type;
                caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
        }
    } else{
        message = "Cannot connect a neuron to itself";
        caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
        //throw "Cannot connect a neuron to itself";
    }
    return false;
}


ConnectionManager::ConnectionManager(caerDeviceHandle h){
    handle = h;
}

map<Neuron,Neuron*> * ConnectionManager::GetNeuronMap(){
    return &(this->neuronMap_);
}

void ConnectionManager::Clear(){
    caerLog(CAER_LOG_NOTICE, __func__,"Clearing Connection Manager...\nAll stored connections will be deleted");
    this->neuronMap_.clear();
}

stringstream ConnectionManager::GetNeuronMapString(){
    stringstream ss;
    
    
    for(auto it = neuronMap_.cbegin(); it != neuronMap_.cend(); ++it)
    {
        Neuron * entry = it->second; 
        ss << "\n";
        ss << entry->GetLocString() << 
        " -- SRAM: " << entry->GetSRAMString() <<
        " -- CAM: " << entry->GetCAMString();  
    }

    return ss;
}

void ConnectionManager::PrintNeuronMap(){   

    string entry_message;
    
    for(auto it = neuronMap_.cbegin(); it != neuronMap_.cend(); ++it)
    {
        Neuron * entry = it->second; 
        entry_message = "\n"+ 
        entry->GetLocString() + 
        " -- SRAM: " + 
        entry->GetSRAMString() +
        " -- CAM: " + 
        entry->GetCAMString();  

        caerLog(CAER_LOG_NOTICE, __func__, entry_message.c_str());
    }

}

vector <Neuron *> ConnectionManager::GetNeuron(Neuron * pre){
    neuronMap_.find(*pre);
}

void ConnectionManager::Connect(Neuron * pre, Neuron * post, uint8_t cam_slots_number, uint8_t connection_type){

    // If already instanciated neuron, use that, otherwise make new entry
    if ( neuronMap_.find(*pre) == neuronMap_.end() ) {
        // New neuron, include in map
        neuronMap_[*pre] = pre;
    } else {
        // Already instantiated, delete and re-reference
        //delete pre;
        pre = neuronMap_[*pre];
    }
    if ( neuronMap_.find(*post) == neuronMap_.end() ) {
        // New neuron, include in map
        neuronMap_[*post] = post;
    } else {
        // Already instantiated, delete and re-reference
        //delete post;
        post = neuronMap_[*post];
    }

    // Attempt to connect
    try{
        if(CheckAndConnect(pre, post, cam_slots_number, connection_type)){
            string message = string("+++ Connected " + pre->GetLocString() + "-" + to_string(connection_type) +
            "-" + to_string(cam_slots_number) + "->" + post->GetLocString()+ "\n");
        caerLog(CAER_LOG_DEBUG, __func__, message.c_str());
        } else{
             string message = string("XXX Did not connect " + pre->GetLocString() + "-" + to_string(connection_type) +
            "-" + to_string(cam_slots_number) + "->" + post->GetLocString()+ "\n");
        caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
        }        
    }
    catch (const string e){
        caerLog(CAER_LOG_NOTICE, __func__, e.c_str());
    }

}

bool ReadNetTXT (ConnectionManager * manager, string filepath) {

    caerLog(CAER_LOG_DEBUG, __func__, ("attempting to read net found at: " + filepath).c_str());
    ifstream netFile (filepath);
    string connection;
    if (netFile.is_open())
    {
        caerLog(CAER_LOG_NOTICE, __func__, ("parsing net found at: " + filepath).c_str());
        vector<uint8_t > cv;
        while ( getline (netFile, connection) )
        {
            if(!connection.empty()){
                // Ignore comments (#)
                if(connection[0] != '#'){
                    size_t prev = 0, pos;
                    // Expected structure is:
                    //     pre_addrss   -cam_slots_number   ->  post_addrss 
                    // ex: U00-C01-N001 -32                 ->  U02-C01-N001 
                    // without tabs: U00-C01-N001-32->U02-C01-N001
                    while ((pos = connection.find_first_of("UCN->", prev)) != string::npos)
                    {
                        if (pos > prev)
                            cv.push_back((unsigned char &&) stoi(connection.substr(prev, pos - prev)));
                        prev = pos+1;
                    }

                    if (prev < connection.length())
                        cv.push_back((unsigned char &&) stoi(connection.substr(prev, string::npos)));
                    manager->Connect(new Neuron(cv[0],cv[1],cv[2]),new Neuron(cv[5],cv[6],cv[7]),cv[4],cv[3]);
                    cv.clear();
                } else{
                    // Print comments in network file that start with #! for debbuging
                    if(connection[1] == '!'){
                        caerLog(CAER_LOG_NOTICE, __func__, ("Printing comment: " + connection + "\n").c_str());
                    }
                }
            }
        }
        netFile.close();
        return true;        

    }
    else{
        caerLog(CAER_LOG_ERROR, __func__, ("unable to open file: " + filepath).c_str());  
        return false;
    } 

}

// TODO: Finish XML reader
bool ReadNetXML (ConnectionManager * manager, string filepath) {

    FILE *netFile;
    mxml_node_t *tree;
    string message = "";
    bool correct_input = true;

    caerLog(CAER_LOG_DEBUG, __func__, ("opening file: " + filepath).c_str());
    netFile = fopen(filepath.c_str(), "r");

    if (netFile == NULL){
        caerLog(CAER_LOG_ERROR, __func__, ("unable to open file: " + filepath).c_str());
        return false;
    }
  

    tree = mxmlLoadFile(NULL, netFile, MXML_NO_CALLBACK);

    if (tree == NULL){
        caerLog(CAER_LOG_ERROR, __func__, ("Invalid XML file"));
        return false;
    }

    const char *name = mxmlGetElement(tree);
    size_t level = 0;
    caerLog(CAER_LOG_DEBUG, __func__, (name));

    mxml_node_t *connections;
    connections = mxmlFindElement(tree, tree, "CONNECTIONS", NULL, NULL, MXML_DESCEND);


    mxml_node_t *current_connection = mxmlGetFirstChild(connections);
    mxml_node_t *pre;
    mxml_node_t *post;

    current_connection = mxmlGetNextSibling(current_connection);

    while (name != NULL){

        message = "";
        correct_input = true;
        
        auto connection_type = (uint8_t)atoi(mxmlElementGetAttr(current_connection, "connection_type"));
       
        
        if (connection_type < 0 || connection_type > 3){ 
            message = string(message + "Connection Type out of range (0-3): " + to_string(connection_type) + "\n");
            correct_input = false;
        }
        
        auto cam_slots_number = (uint8_t)atoi(mxmlElementGetAttr(current_connection, "cam_slots_number"));
       
        if (cam_slots_number < 0 || cam_slots_number > 64){ 
            message = string(message + "CAM slot number out of range (0-64): " + to_string(cam_slots_number) + "\n");
            correct_input = false;
        }

       
        pre = mxmlFindElement(current_connection, current_connection, "PRE", NULL, NULL, MXML_DESCEND);
        if (pre == NULL){ 
            message = string(message + "Each connection should have a PRE neuron children\n");
            correct_input = false;
        }

        auto pre_chip = (uint8_t)atoi(mxmlElementGetAttr(pre, "CHIP"));
        if (pre_chip < 0 || pre_chip > 3){ 
            message = string(message + "Pre-Synaptic chip out of range (0-3): " + to_string(pre_chip)+ "\n");
            correct_input = false;
        }


        auto pre_core = (uint8_t)atoi(mxmlElementGetAttr(pre, "CORE"));
        if (pre_core < 0 || pre_core > 3){ 
            message = string(message + "Pre-Synaptic core out of range (0-3): " + to_string(pre_core)+ "\n");
            correct_input = false;
        }

        auto pre_neuron = (uint8_t)atoi(mxmlElementGetAttr(pre, "NEURON"));
        if (pre_neuron < 0 || pre_neuron > 255){ 
            message = string(message + "Pre-Synaptic neuron out of range (0-255): " + to_string(pre_neuron)+ "\n");
            correct_input = false;
        }

        post = mxmlFindElement(current_connection, current_connection, "POST", NULL, NULL, MXML_DESCEND);

        auto post_chip = (uint8_t)atoi(mxmlElementGetAttr(post, "CHIP"));
        if (post_chip < 0 || post_chip > 3){ 
            message = string(message + "Post-Synaptic chip out of range (0-3): " + to_string(post_chip)+ "\n");
            correct_input = false;
        }


        auto post_core = (uint8_t)atoi(mxmlElementGetAttr(post, "CORE"));
        if (post_core < 0 || post_core > 3){ 
            message = string(message + "Post-Synaptic core out of range (0-3): " + to_string(post_core)+ "\n");
            correct_input = false;
        }

        auto post_neuron = (uint8_t)atoi(mxmlElementGetAttr(post, "NEURON"));
        if (post_neuron < 0 || post_neuron > 255){ 
            message = string(message + "Post-Synaptic neuron out of range (0-255): " + to_string(post_neuron)+ "\n");
            correct_input = false;
        }

        
        // Make connection
        if(correct_input){
            manager->Connect(
            new Neuron(pre_chip, pre_core, pre_neuron),
            new Neuron(post_chip, post_core, post_neuron),
            cam_slots_number, connection_type);
        }
        else{
             caerLog(CAER_LOG_NOTICE, __func__, string("Incorrect Input: " + message).c_str());
        }
       

        // Loop to next connection 
        // TODO: figure out why I have to do this twice
        current_connection = mxmlGetNextSibling(current_connection);
        current_connection = mxmlGetNextSibling(current_connection);
        name = mxmlGetElement(current_connection);
    }

 
    fclose(netFile);

}

