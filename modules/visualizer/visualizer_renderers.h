/*
 * visualizer_renderers.h
 *
 *  Created on: Apr 5, 2017
 *      Author: llongi
 */

#ifndef MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_
#define MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_

#include "visualizer.h"

// Default renderers.
bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container,
	bool doClear);
bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container,
	bool doClear);
bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container, bool doClear);
bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container,
	bool doClear);
bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container,
	bool doClear);
bool caerVisualizerRendererSpikeEventsRaster(caerVisualizerPublicState state, caerEventPacketContainer container,
bool doClear);
bool caerVisualizerRendererETF4D(caerVisualizerPublicState state, caerEventPacketContainer container,
bool doClear);

// Default multi renderers.
bool caerVisualizerMultiRendererPolarityAndFrameEvents(caerVisualizerPublicState state,
	caerEventPacketContainer container,
	bool doClear);

#endif /* MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_ */
