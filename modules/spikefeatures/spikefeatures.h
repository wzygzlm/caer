/*
 * backgroundactivityfilter.h
 *
 *  Created on: May 1, 2017
 *      Author: federico @ Capo Caccia with Andre'
 */

#ifndef SPIKEFEATURESFILTER_H_
#define SPIKEFEATURESFILTER_H_

#include "main.h"

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

#define num_features_map 50
#define map_size 11

void caerSpikeFeatures(uint16_t moduleID, caerPolarityEventPacket polarity);
void caerSpikeFeaturesMakeFrame(uint16_t moduleID, caerFrameEventPacket *imagegeneratorFrame);


#endif /* SPIKEFEATURESFILTER_H_ */
