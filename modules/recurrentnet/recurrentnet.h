/*
 * recurrentnet.h
 *
 *  Created on: Capo Caccia 2017
 *      Author: federico
 */

#ifndef RECURRENTNET_H_
#define RECURRENTNET_H_

#include "main.h"
#include "modules/ini/dynapse_common.h"	// useful constants

#include <libcaer/events/spike.h>

void caerRecurrentNet(uint16_t moduleID, caerSpikeEventPacket spike);

#endif /* RECURRENTNET_H_ */
