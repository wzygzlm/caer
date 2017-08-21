#ifndef MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_
#define MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_

#include "visualizer.hpp"

// Default renderers.
bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container);
bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
bool caerVisualizerRendererSpikeEventsRaster(caerVisualizerPublicState state, caerEventPacketContainer container);
bool caerVisualizerRendererETF4D(caerVisualizerPublicState state, caerEventPacketContainer container);

// Default multi renderers.
bool caerVisualizerMultiRendererPolarityAndFrameEvents(caerVisualizerPublicState state,
	caerEventPacketContainer container);

typedef bool (*caerVisualizerRenderer)(caerVisualizerPublicState state, caerEventPacketContainer container);

typedef void *(*caerVisualizerRendererStateInit)(caerVisualizerPublicState state);
typedef void (*caerVisualizerRendererStateExit)(caerVisualizerPublicState state);

struct caer_visualizer_renderer_info {
	const std::string name;
	caerVisualizerRenderer renderer;
	bool needsOpenGL3;
	caerVisualizerRendererStateInit stateInit;
	caerVisualizerRendererStateExit stateExit;
};

typedef const struct caer_visualizer_renderer_info *caerVisualizerRendererInfo;

static const std::string caerVisualizerRendererListOptionsString =
	"None,Polarity,Frame,IMU_6-axes,2D_Points,Spikes,Spikes_Raster_Plot,ETF4D,Polarity_and_Frames";

static const struct caer_visualizer_renderer_info caerVisualizerRendererList[] = { { "None", nullptr }, { "Polarity",
	&caerVisualizerRendererPolarityEvents }, { "Frame", &caerVisualizerRendererFrameEvents }, { "IMU_6-axes",
	&caerVisualizerRendererIMU6Events }, { "2D_Points", &caerVisualizerRendererPoint2DEvents }, { "Spikes",
	&caerVisualizerRendererSpikeEvents }, { "Spikes_Raster_Plot", &caerVisualizerRendererSpikeEventsRaster }, { "ETF4D",
	&caerVisualizerRendererETF4D }, { "Polarity_and_Frames", &caerVisualizerMultiRendererPolarityAndFrameEvents }, };

#endif /* MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_ */
