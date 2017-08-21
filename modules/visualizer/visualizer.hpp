#ifndef VISUALIZER_H_
#define VISUALIZER_H_

#include "main.h"

#include <libcaer/events/packetContainer.h>
#include <GL/glew.h>
#include <SFML/Graphics.hpp>

struct caer_visualizer_public_state {
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	uint32_t renderSizeX;
	uint32_t renderSizeY;
	sf::RenderWindow *renderWindow;
	void *renderState; // Reserved for renderers to put their internal state into. Must allocate with malloc() family, free is automatic.
	sf::Font *font;
};

typedef struct caer_visualizer_public_state *caerVisualizerPublicState;

typedef bool (*caerVisualizerRenderer)(caerVisualizerPublicState state, caerEventPacketContainer container);
typedef void (*caerVisualizerEventHandler)(caerVisualizerPublicState state, sf::Event &event);

#endif /* VISUALIZER_H_ */
