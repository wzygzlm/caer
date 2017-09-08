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
        else if (chip_to == 1){return bits = {1,1,0,0};}
        else if (chip_to == 2){return bits = {0,0,0,1};}
        else if (chip_to == 3){return bits = {1,1,0,1};}
    }
    else if (chip_from == 1){
        if (chip_to == 0){return bits = {0,1,0,0};}
        else if (chip_to == 1){return bits = {0,0,0,0};}
        else if (chip_to == 2){return bits = {0,1,0,1};}
        else if (chip_to == 3){return bits = {0,0,0,1};}
    }
    else if (chip_from == 2){
        if (chip_to == 0){return bits = {0,0,1,1};}
        else if (chip_to == 1){return bits = {1,1,1,1};}
        else if (chip_to == 2){return bits = {0,0,0,0};}
        else if (chip_to == 3){return bits = {1,1,0,0};}
    }
    else if (chip_from == 3){
        if (chip_to == 0){return bits = {0,1,1,1};}
        else if (chip_to == 1){return bits = {0,0,1,1};}
        else if (chip_to == 2){return bits = {0,1,0,0};}
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
}

// For CAM
uint32_t ConnectionManager::NeuronCamAddress(int neuron, int core){
    return (uint32_t) neuron + core*256;
}


// The Connection manager keeps track of the SRAM and CAM registers of all neurons
// involved in a connection (sparse). Since there is no real way to access the
// registers themselves in order for this to work you must piping all connection settings through
// pipe all your connection settings through this manager (don't call caerDynapseWriteSram/Cam directly)
void ConnectionManager::MakeConnection( Neuron * pre, Neuron * post, uint8_t syn_strength, uint8_t connection_type ){

    // In internal map
    pre->SRAM.push_back(post);
    post->CAM.push_back(pre);


    vector<uint8_t> dirBits = CalculateBits(pre->chip, post->chip);

    // print SRAM command
    string message = "SRAM Settings: "+
    to_string(pre->chip)+ ", " + 
    to_string(pre->core)+ ", " + 
    to_string(pre->neuron)+ ", " +   
    to_string(pre->core)+ ", " +
    to_string((bool)dirBits[0])+ ", " +
    to_string(dirBits[1])+ ", " +
    to_string((bool)dirBits[2])+ ", " +
    to_string(dirBits[3])+ ", " +
    to_string(pre->SRAM.size())+ ", " +
    string(to_string(GetDestinationCore(post->core)));

    caerLog(CAER_LOG_NOTICE, __func__, message.c_str());


    // Program SRAM
    caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, pre->chip);
    caerDynapseWriteSram(handle, pre->core, pre->neuron, pre->core, (bool)dirBits[0],
                         dirBits[1], (bool)dirBits[2], dirBits[3], (uint16_t) pre->SRAM.size()-1, GetDestinationCore(post->core));


    message = "CAM Settings: "+ 
    to_string(post->chip)+ ", " +
    to_string(NeuronCamAddress(pre->neuron,pre->core))+ ", " +
    to_string(NeuronCamAddress(post->neuron,post->core))+ ", " +
    to_string(post->CAM.size())+ ", " +
    to_string(DYNAPSE_CONFIG_CAMTYPE_F_EXC);

    caerLog(CAER_LOG_NOTICE, __func__, message.c_str());

    // Program CAM
    // TODO: Allow multiple CAM id per connection
    caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, post->chip);
    caerDynapseWriteCam(handle, NeuronCamAddress(pre->neuron,pre->core), NeuronCamAddress(post->neuron,post->core),
                        (uint32_t) post->CAM.size()-1, DYNAPSE_CONFIG_CAMTYPE_F_EXC);



}

void ConnectionManager::CheckAndConnect(Neuron * pre, Neuron * post, uint8_t syn_strength, uint8_t connection_type ){
    string message = string("Attempting to connect " + pre->GetLocString() + "->" + post->GetLocString());
    caerLog(CAER_LOG_NOTICE, __func__, message.c_str());

    if(!(*pre == *post)) {
        if (pre->SRAM.size() < 4) {
            if (post->CAM.size() > 0) {
                if (post->CAM.size() < 64) {

                    //find instances where contents in the cam will clash with the new element being added
                    auto it = post->FindCamClash(pre);

                    //if no clashes, connect
                    if (it == post->CAM.end()) {
                        caerLog(CAER_LOG_NOTICE, __func__, "Passed tests");
                        MakeConnection(pre, post, syn_strength, connection_type);

                    } else {
                        message = string("CAM Clash at " + post->GetLocString() + " between " + (*it)->GetLocString() + " and " + pre->GetLocString());
                        caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
                        //throw message;
                        
                    }

                } else {
                    message = "CAM Size Limit (64) Reached for: " + post->GetLocString();
                    caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
                    //throw "CAM Size Limit (64) Reached: " + post->GetLocString();
                }
            } else {
                //If CAM is empty, connect
                MakeConnection(pre, post, syn_strength, connection_type);
            };
        } else {
            message = "SRAM Size Limit (4) Reached: " + pre->GetLocString();
            caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
            //throw "SRAM Size Limit (4) Reached: " + pre->GetLocString();
        }
    } else{
        message = "Cannot connect a neuron to itself";
        caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
        //throw "Cannot connect a neuron to itself";
    }
}


ConnectionManager::ConnectionManager(caerDeviceHandle h){
    handle = h;
}

map<Neuron,Neuron*> * ConnectionManager::GetNeuronMap(){
    return &(this->neuronMap_);
}

void ConnectionManager::PrintNeuronMap(){
    stringstream ss;
    
    
    for(auto it = neuronMap_.cbegin(); it != neuronMap_.cend(); ++it)
    {
        Neuron * entry = it->second; 
        ss << "\n";
        ss << entry->GetLocString() << 
        " -- SRAM: " << entry->GetSRAMString() <<
        " -- CAM: " << entry->GetCAMString();  
    }

    caerLog(CAER_LOG_NOTICE, __func__, ss.str().c_str());
}

vector <Neuron *> ConnectionManager::GetNeuron(Neuron * pre){
    neuronMap_.find(*pre);
}

void ConnectionManager::Connect(Neuron * pre, Neuron * post, uint8_t syn_strength, uint8_t connection_type){

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
        CheckAndConnect(pre, post, syn_strength, connection_type);
        string message = string("Connected " + pre->GetLocString() + "->" + post->GetLocString());
        caerLog(CAER_LOG_NOTICE, __func__, message.c_str());
        //cout << "Connected " + pre->GetLocString() + "->" + post->GetLocString() << endl;
    }
    catch (const string e){
        caerLog(CAER_LOG_NOTICE, __func__, e.c_str());
    }

}



void ReadNet (ConnectionManager manager, string filepath) {

    caerLog(CAER_LOG_NOTICE, __func__, ("attempting to read net found at: " + filepath).c_str());
    ifstream netFile (filepath);
    string connection;
    if (netFile.is_open())
    {
        caerLog(CAER_LOG_NOTICE, __func__, ("parsing net found at: " + filepath).c_str());
        vector<uint8_t > cv;
        while ( getline (netFile,connection) )
        {
            size_t prev = 0, pos;
            while ((pos = connection.find_first_of("UCN->", prev)) != string::npos)
            {
                if (pos > prev)
                    cv.push_back((unsigned char &&) stoi(connection.substr(prev, pos - prev)));
                prev = pos+1;
            }

            if (prev < connection.length())
                cv.push_back((unsigned char &&) stoi(connection.substr(prev, string::npos)));
            manager.Connect(new Neuron(cv[0],cv[1],cv[2]),new Neuron(cv[3],cv[4],cv[5]),1,1);
            cv.clear();
        }
        netFile.close();
        manager.PrintNeuronMap();

    }
    else caerLog(CAER_LOG_NOTICE, __func__, ("unable to open file: " + filepath).c_str());

}

// TODO: Finish XML reader
void ReadXMLNet ( string filepath) {

    FILE *fp;
    mxml_node_t *tree;

    fp = fopen(filepath.c_str(), "r");
    cout << "opened xml file";
    tree = mxmlLoadFile(NULL, fp, MXML_NO_CALLBACK);
    fclose(fp);


//    ifstream netFile (filepath);
//    string connection;
//    if (netFile.is_open())
//    {
//        vector<uint8_t > cv;
//        while ( getline (netFile,connection) )
//        {
//            size_t prev = 0, pos;
//            while ((pos = connection.find_first_of("UCN->", prev)) != string::npos)
//            {
//                if (pos > prev)
//                    cv.push_back(stoi(connection.substr(prev, pos-prev)));
//                prev = pos+1;
//            }
//
//            if (prev < connection.length())
//                cv.push_back(stoi(connection.substr(prev, string::npos)));
//            manager.Connect(new Neuron(cv[0],cv[1],cv[2]),new Neuron(cv[3],cv[4],cv[5]),1,1);
//            cv.clear();
//        }
//        netFile.close();
//
//    }
//    else cout << "Unable to open file";

}

void ExampleXMLsave(){

    FILE *fp;
    mxml_node_t *tree;

    fp = fopen("filename.xml", "w");
    mxmlSaveFile(tree, fp, MXML_NO_CALLBACK);
    fclose(fp);
}


