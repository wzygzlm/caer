/*
 * poissonspikegen.h
 *
 *  Created on: May 2017 for the poisson generator on dynap-se
 *      Author: Carsten
 */

#ifndef POISSONSPIKEGEN_H_
#define POISSONSPIKEGEN_H_

#include "main.h"
#include "modules/ini/dynapse_common.h"	// useful constants

#include <libcaer/events/spike.h>

void caerPoissonSpikeGenModule(uint16_t moduleID, caerSpikeEventPacket spike);

#endif /* POISSONSPIKEGEN_H_ */
