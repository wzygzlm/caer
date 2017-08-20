#ifndef MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_
#define MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_

#include "visualizer.hpp"

// Default event handlers.
void caerVisualizerEventHandlerSpikeEvents(caerVisualizerPublicState state, sf::Event &event);
void caerVisualizerEventHandlerInput(caerVisualizerPublicState state, sf::Event &event);

struct caer_visualizer_handlers {
	const std::string name;
	caerVisualizerEventHandler handler;
};

static const std::string caerVisualizerHandlerListOptionsString = "None,Spikes,Input";

static struct caer_visualizer_handlers caerVisualizerHandlerList[] = { { "None", nullptr }, { "Spikes",
	&caerVisualizerEventHandlerSpikeEvents }, { "Input", &caerVisualizerEventHandlerInput } };

#endif /* MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_ */
