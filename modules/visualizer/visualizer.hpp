#ifndef VISUALIZER_H_
#define VISUALIZER_H_

#include "main.h"
#include "base/module.h"

#include <libcaer/events/packetContainer.h>
#include <SFML/Graphics.hpp>

#define VISUALIZER_DEFAULT_ZOOM 2.0f
#define VISUALIZER_REFRESH_RATE 60
#define VISUALIZER_DEFAULT_POSITION_X 40
#define VISUALIZER_DEFAULT_POSITION_Y 40

struct caer_visualizer_public_state {
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	uint32_t renderSizeX;
	uint32_t renderSizeY;
	sf::RenderWindow *renderWindow;
	sf::Font *font;
};

typedef struct caer_visualizer_public_state *caerVisualizerPublicState;
typedef struct caer_visualizer_state *caerVisualizerState;

typedef bool (*caerVisualizerRenderer)(caerVisualizerPublicState state, caerEventPacketContainer container);
typedef void (*caerVisualizerEventHandler)(caerVisualizerPublicState state, sf::Event &event);

#endif /* VISUALIZER_H_ */
