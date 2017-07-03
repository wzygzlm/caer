#ifndef VISUALIZER_H_
#define VISUALIZER_H_

#include "main.h"
#include "base/module.h"

#include <libcaer/events/packetContainer.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_font.h>

#define VISUALIZER_DEFAULT_ZOOM 2.0f
#define VISUALIZER_REFRESH_RATE 60.0f
#define VISUALIZER_DEFAULT_POSITION_X 40
#define VISUALIZER_DEFAULT_POSITION_Y 40

struct caer_visualizer_public_state {
	void *eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	int32_t bitmapRendererSizeX;
	int32_t bitmapRendererSizeY;
	ALLEGRO_FONT *displayFont;
};

typedef struct caer_visualizer_public_state *caerVisualizerPublicState;
typedef struct caer_visualizer_state *caerVisualizerState;

typedef bool (*caerVisualizerRenderer)(caerVisualizerPublicState state, caerEventPacketContainer container,
	bool doClear);
typedef void (*caerVisualizerEventHandler)(caerVisualizerPublicState state, ALLEGRO_EVENT event);

// For reuse inside other modules.
caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	int32_t bitmapSizeX, int32_t bitmapSizeY, float defaultZoomFactor, bool defaultShowStatistics,
	caerModuleData parentModule, int16_t eventSourceID);
void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container);
void caerVisualizerExit(caerVisualizerState state);
void caerVisualizerReset(caerVisualizerState state);

void caerVisualizerSystemInit(void);

#endif /* VISUALIZER_H_ */
