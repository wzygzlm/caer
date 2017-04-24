/*
 * fpgaspikegen.h
 *
 */

#ifndef FPGASPIKEGEN_H_
#define FPGASPIKEGEN_H_

#include "main.h"
#include "modules/ini/dynapse_common.h"	// useful constants

#include <libcaer/events/spike.h>

void caerFpgaSpikeGenModule(uint16_t moduleID, caerSpikeEventPacket spike);

#endif /* FPGASPIKEGEN_H_ */
