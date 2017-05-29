#ifndef SYNAPSERECONFIG_H_

#define SYNAPSERECONFIG_H_

#include "main.h"

#include <libcaer/events/spike.h>

void caerSynapseReconfigModule(uint16_t moduleID, caerSpikeEventPacket spike);

#endif /* SYNAPSERECONFIG_H_ */
