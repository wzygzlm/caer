#ifndef MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_
#define MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_

#include "visualizer.hpp"

// Default event handlers.
void caerVisualizerEventHandlerSpikeEvents(caerVisualizerPublicState state, const sf::Event &event);
void caerVisualizerEventHandlerInput(caerVisualizerPublicState state, const sf::Event &event);

typedef void (*caerVisualizerEventHandler)(caerVisualizerPublicState state, const sf::Event &event);

struct caer_visualizer_event_handler_info {
	const std::string name;
	caerVisualizerEventHandler eventHandler;
};

typedef const struct caer_visualizer_event_handler_info *caerVisualizerEventHandlerInfo;

static const std::string caerVisualizerEventHandlerListOptionsString = "None,Spikes,Input";

static const struct caer_visualizer_event_handler_info caerVisualizerEventHandlerList[] = { { "None", nullptr },
	{ "Spikes", &caerVisualizerEventHandlerSpikeEvents }, { "Input", &caerVisualizerEventHandlerInput } };

#endif /* MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_ */
