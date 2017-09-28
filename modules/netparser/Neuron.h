//
// Created by rodrigo on 5/30/17.
//

#ifndef CAER_NEURON_H
#define CAER_NEURON_H

#endif //CAER_NEURON_H

//
// Created by rodrigo on 5/30/17.
//

#include <iostream>
#include <map>
#include <vector>
#include <iomanip>
#include <string>
#include <sstream>
#include <functional>
#include <set>
#include <fstream>
#include <mxml.h>
#include <libcaer/devices/dynapse.h>
#include "base/mainloop.h"

using namespace std;

struct  Neuron {
    const uint8_t chip;
    const uint8_t core;
    const uint8_t neuron;
    vector<Neuron *> SRAM;
    vector<Neuron *> CAM;
    vector<uint8_t> synapse_type;

    Neuron(uint8_t chip_n ,uint8_t core_n ,uint8_t neuron_n);
    Neuron();
    string GetLocString() const;
    void Print() const;
    void PrintSRAM();
    void PrintCAM();
    string GetSRAMString();
    string GetCAMString();
    vector<Neuron *>::iterator FindCamClash(Neuron * n);
};

class CamClashPred{
private:
    Neuron* neuronA_;
public:
    CamClashPred(Neuron* neuronA_);
    bool operator()(const Neuron* neuronB);
};

// Make neuron object comparable
bool operator < (const Neuron& x, const Neuron& y) ;
bool operator > (const Neuron& x, const Neuron& y) ;
bool operator == (const Neuron& x, const Neuron& y) ;



// Class that manages all connections
class ConnectionManager {
private:

    map< Neuron, Neuron* > neuronMap_;
    caerDeviceHandle handle;

    // Hard coded bit vectors for SRAM connections
    vector<uint8_t> CalculateBits(int chip_from, int chip_to);
    uint16_t GetDestinationCore(int core);

    // For CAM
    uint32_t NeuronCamAddress(int neuron, int core);

    // Checks for valid connection and calls MakeConnection
    bool CheckAndConnect(Neuron *pre, Neuron *post, uint8_t syn_strength, uint8_t connection_type);

    // Appends connection to software SRAM and CAM and calls caerDynapseWriteSram and caerDynapseWriteCam
    void MakeConnection(Neuron *pre, Neuron *post, uint8_t syn_strength, uint8_t connection_type);

public:
    ConnectionManager(caerDeviceHandle h);
    
    void Clear();

    map<Neuron, Neuron *> *GetNeuronMap();

    void PrintNeuronMap();
    stringstream GetNeuronMapString();

    vector<Neuron *> GetNeuron(Neuron *pre);

    // Checks for valid connection and calls MakeConnection
    // TODO: Implement syn_strength and connection type
    void Connect(Neuron *pre, Neuron *post, uint8_t syn_strength, uint8_t connection_type);

};

// Reads net from txt file in format U02-C02-N002->U02-C02-N006
bool ReadNetTXT (ConnectionManager * manager, string filepath) ;

// Reads net from XML file in format
//<CONNECTIONS num="5">
//  <CONNECTION connection_type="1" syn_stength="1">
//     <PRE CHIP="1" CORE="1" NEURON="1" />
//     <POST CHIP="2" CORE="2" NEURON="2" />
//  </CONNECTION>
//</CONNECTIONS>
bool ReadNetXML (ConnectionManager * manager, string filepath) ;
