/*
 * visualizer_handlers.h
 *
 *  Created on: Apr 5, 2017
 *      Author: llongi
 */

#ifndef MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_
#define MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_

#include "visualizer.h"

// Default event handlers.
void caerVisualizerEventHandlerSpikeEvents(caerVisualizerPublicState state, ALLEGRO_EVENT event);
void caerVisualizerEventHandlerFrameEvents(caerVisualizerPublicState state, ALLEGRO_EVENT event);

#endif /* MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_ */
