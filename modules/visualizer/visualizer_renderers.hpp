#ifndef MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_
#define MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_

#include "visualizer.hpp"

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

extern const std::string caerVisualizerRendererListOptionsString;
extern const struct caer_visualizer_renderer_info caerVisualizerRendererList[];
extern const size_t caerVisualizerRendererListLength;

#endif /* MODULES_VISUALIZER_VISUALIZER_RENDERERS_H_ */
