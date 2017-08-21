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
	void *renderState; // Reserved for renderers to put their internal state into.
	sf::RenderWindow *renderWindow;
	sf::Font *font;
};

typedef const struct caer_visualizer_public_state *caerVisualizerPublicState;

#endif /* VISUALIZER_H_ */
