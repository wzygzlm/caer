#ifndef MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_
#define MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_

#include "visualizer.hpp"

// Default event handlers.
void caerVisualizerEventHandlerSpikeEvents(caerVisualizerPublicState state, sf::Event &event);
void caerVisualizerEventHandlerFrameEvents(caerVisualizerPublicState state, sf::Event &event);
void caerInputVisualizerEventHandler(caerVisualizerPublicState state, sf::Event &event);

#endif /* MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_ */
