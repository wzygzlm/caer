#include "visualizer.hpp"
#include "base/mainloop.h"
#include "ext/ringbuffer/ringbuffer.h"
#include "ext/threads_ext.h"
#include "ext/resources/LiberationSans-Bold.h"
#include "modules/statistics/statistics.h"

#include "visualizer_handlers.hpp"
#include "visualizer_renderers.hpp"

#include <atomic>
#include <thread>
#include <mutex>

#if defined(OS_LINUX) && OS_LINUX == 1
	#include <X11/Xlib.h>
#endif

static caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	uint32_t sizeX, uint32_t sizeY, float defaultZoomFactor, bool defaultShowStatistics, caerModuleData parentModule,
	int16_t eventSourceID);
static void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container);
static void caerVisualizerExit(caerVisualizerState state);
static void caerVisualizerReset(caerVisualizerState state);
static void caerVisualizerSystemInit(void);

static std::once_flag visualizerSystemIsInitialized;

struct caer_visualizer_renderers {
	const char *name;
	caerVisualizerRenderer renderer;
};

static const char *caerVisualizerRendererListOptionsString =
	"Polarity,Frame,IMU_6-axes,2D_Points,Spikes,Spikes_Raster_Plot,ETF4D,Polarity_and_Frames";

static struct caer_visualizer_renderers caerVisualizerRendererList[] = { { "Polarity",
	&caerVisualizerRendererPolarityEvents }, { "Frame", &caerVisualizerRendererFrameEvents }, { "IMU_6-axes",
	&caerVisualizerRendererIMU6Events }, { "2D_Points", &caerVisualizerRendererPoint2DEvents }, { "Spikes",
	&caerVisualizerRendererSpikeEvents }, { "Spikes_Raster_Plot", &caerVisualizerRendererSpikeEventsRaster }, { "ETF4D",
	&caerVisualizerRendererETF4D }, { "Polarity_and_Frames", &caerVisualizerMultiRendererPolarityAndFrameEvents }, };

struct caer_visualizer_handlers {
	const char *name;
	caerVisualizerEventHandler handler;
};

static const char *caerVisualizerHandlerListOptionsString = "None,Spikes,Input";

static struct caer_visualizer_handlers caerVisualizerHandlerList[] = { { "None", nullptr }, { "Spikes",
	&caerVisualizerEventHandlerSpikeEvents }, { "Input", &caerInputVisualizerEventHandler } };

struct caer_visualizer_state {
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	uint32_t renderSizeX;
	uint32_t renderSizeY;
	sf::RenderWindow *renderWindow;
	sf::Font *font;
	std::atomic_bool running;
	std::atomic_bool windowResize;
	std::atomic_bool windowMove;
	RingBuffer dataTransfer;
	std::thread renderingThread;
	caerVisualizerRenderer renderer;
	caerVisualizerEventHandler eventHandler;
	caerModuleData parentModule;
	bool showStatistics;
	struct caer_statistics_state packetStatistics;
	std::atomic_uint_fast32_t packetSubsampleRendering;
	uint32_t packetSubsampleCount;
};

static void updateDisplaySize(caerVisualizerState state);
static void updateDisplayLocation(caerVisualizerState state);
static void saveDisplayLocation(caerVisualizerState state);
static void caerVisualizerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static bool caerVisualizerInitGraphics(caerVisualizerState state);
static void caerVisualizerHandleEvents(caerVisualizerState state);
static void caerVisualizerUpdateScreen(caerVisualizerState state);
static void caerVisualizerExitGraphics(caerVisualizerState state);
static int caerVisualizerRenderThread(void *visualizerState);

#define GLOBAL_FONT_SIZE 20 // in pixels
#define GLOBAL_FONT_SPACING 5 // in pixels

// Calculated at system init.
static uint32_t STATISTICS_WIDTH = 0;
static uint32_t STATISTICS_HEIGHT = 0;

static void caerVisualizerSystemInit(void) {
	// Call XInitThreads() on Linux.
#if defined(OS_LINUX) && OS_LINUX == 1
	XInitThreads();
#endif

	// Determine biggest possible statistics string.
	size_t maxStatStringLength = (size_t) snprintf(nullptr, 0, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);

	char maxStatString[maxStatStringLength + 1];
	snprintf(maxStatString, maxStatStringLength + 1, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);
	maxStatString[maxStatStringLength] = '\0';

	// Load statistics font into memory.
	sf::Font font;
	if (!font.loadFromMemory(LiberationSans_Bold_ttf, LiberationSans_Bold_ttf_len)) {
		caerLog(CAER_LOG_ERROR, "Visualizer", "Failed to load display font.");
	}

	// Determine statistics string width.
	sf::Text maxStatText(maxStatString, font, GLOBAL_FONT_SIZE);
	STATISTICS_WIDTH = (2 * GLOBAL_FONT_SPACING) + U32T(maxStatText.getLocalBounds().width);

	STATISTICS_HEIGHT = (3 * GLOBAL_FONT_SPACING) + (2 * U32T(maxStatText.getLocalBounds().height));
}

caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	uint32_t sizeX, uint32_t sizeY, float defaultZoomFactor, bool defaultShowStatistics, caerModuleData parentModule,
	int16_t eventSourceID) {
	// Initialize visualizer framework (global font sizes). Do only once per startup!
	std::call_once(visualizerSystemIsInitialized, &caerVisualizerSystemInit);

	// Allocate memory for visualizer state.
	caerVisualizerState state = (caerVisualizerState) calloc(1, sizeof(struct caer_visualizer_state));
	if (state == nullptr) {
		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to allocate state memory.");
		return (nullptr);
	}

	state->parentModule = parentModule;
	state->visualizerConfigNode = parentModule->moduleNode;
	if (eventSourceID >= 0) {
		state->eventSourceConfigNode = caerMainloopGetSourceNode(eventSourceID);
	}

	// Configuration.
	sshsNodeCreateInt(parentModule->moduleNode, "subsampleRendering", 1, 1, 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Speed-up rendering by only taking every Nth EventPacketContainer to render.");
	sshsNodeCreateBool(parentModule->moduleNode, "showStatistics", defaultShowStatistics, SSHS_FLAGS_NORMAL,
		"Show event statistics above content (top of window).");
	sshsNodeCreateFloat(parentModule->moduleNode, "zoomFactor", defaultZoomFactor, 0.5f, 50.0f, SSHS_FLAGS_NORMAL,
		"Content zoom factor.");
	sshsNodeCreateInt(parentModule->moduleNode, "windowPositionX", VISUALIZER_DEFAULT_POSITION_X, 0, UINT16_MAX,
		SSHS_FLAGS_NORMAL, "Position of window on screen (X coordinate).");
	sshsNodeCreateInt(parentModule->moduleNode, "windowPositionY", VISUALIZER_DEFAULT_POSITION_Y, 0, UINT16_MAX,
		SSHS_FLAGS_NORMAL, "Position of window on screen (Y coordinate).");

	state->packetSubsampleRendering.store(U32T(sshsNodeGetInt(parentModule->moduleNode, "subsampleRendering")));

	// Remember sizes.
	state->renderSizeX = sizeX;
	state->renderSizeY = sizeY;

	// Remember rendering and event handling function.
	state->renderer = renderer;
	state->eventHandler = eventHandler;

	// Enable packet statistics.
	if (!caerStatisticsStringInit(&state->packetStatistics)) {
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to initialize statistics string.");
		return (nullptr);
	}

	// Initialize ring-buffer to transfer data to render thread.
	state->dataTransfer = ringBufferInit(64);
	if (state->dataTransfer == nullptr) {
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to initialize ring-buffer.");
		return (nullptr);
	}

#if defined(OS_MACOSX) && OS_MACOSX == 1
	// On OS X, creation (and destruction) of the window, as well as its event
	// handling must happen on the main thread. Only drawing can be separate.
	if (!caerVisualizerInitGraphics(state)) {
		ringBufferFree(state->dataTransfer);
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to start rendering thread.");
		return (nullptr);
	}
#endif

	// Start separate rendering thread. Decouples presentation from
	// data processing and preparation. Communication over ring-buffer.
	state->running.store(true);

	try {
		state->renderingThread = std::thread(&caerVisualizerRenderThread, state);
	}
	catch (const std::system_error &) {
#if defined(OS_MACOSX) && OS_MACOSX == 1
		// On OS X, creation (and destruction) of the window, as well as its event
		// handling must happen on the main thread. Only drawing can be separate.
		caerVisualizerExitGraphics(state);
#endif

		ringBufferFree(state->dataTransfer);
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to start rendering thread.");
		return (nullptr);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(parentModule->moduleNode, state, &caerVisualizerConfigListener);

	caerModuleLog(parentModule, CAER_LOG_DEBUG, "Visualizer: Initialized successfully.");

	return (state);
}

static void updateDisplayLocation(caerVisualizerState state) {
	// Set current position to what is in configuration storage.
	const sf::Vector2i newPos(sshsNodeGetInt(state->parentModule->moduleNode, "windowPositionX"),
		sshsNodeGetInt(state->parentModule->moduleNode, "windowPositionY"));

	state->renderWindow->setPosition(newPos);
}

static void saveDisplayLocation(caerVisualizerState state) {
	const sf::Vector2i currPos = state->renderWindow->getPosition();

	// Update current position in configuration storage.
	sshsNodePutInt(state->parentModule->moduleNode, "windowPositionX", currPos.x);
	sshsNodePutInt(state->parentModule->moduleNode, "windowPositionY", currPos.y);
}

static void updateDisplaySize(caerVisualizerState state) {
	state->showStatistics = sshsNodeGetBool(state->parentModule->moduleNode, "showStatistics");
	float zoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

	sf::Vector2u newRenderWindowSize(state->renderSizeX, state->renderSizeY);

	// When statistics are turned on, we need to add some space to the
	// X axis for displaying the whole line and the Y axis for spacing.
	if (state->showStatistics) {
		if (STATISTICS_WIDTH > newRenderWindowSize.x) {
			newRenderWindowSize.x = STATISTICS_WIDTH;
		}

		newRenderWindowSize.y += STATISTICS_HEIGHT;
	}

	// Set view size to render area.
	state->renderWindow->setView(sf::View(sf::FloatRect(0, 0, newRenderWindowSize.x, newRenderWindowSize.y)));

	// Apply zoom to all content.
	newRenderWindowSize.x *= zoomFactor;
	newRenderWindowSize.y *= zoomFactor;

	// Set window size to zoomed area.
	state->renderWindow->setSize(newRenderWindowSize);
}

static void caerVisualizerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerVisualizerState state = (caerVisualizerState) userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_FLOAT && caerStrEquals(changeKey, "zoomFactor")) {
			// Set resize flag.
			state->windowResize.store(true);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "showStatistics")) {
			// Set resize flag. This will then also update the showStatistics flag, ensuring
			// statistics are never shown without the screen having been properly resized first.
			state->windowResize.store(true);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "subsampleRendering")) {
			state->packetSubsampleRendering.store(U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "windowPositionX")) {
			// Set move flag.
			state->windowMove.store(true);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "windowPositionY")) {
			// Set move flag.
			state->windowMove.store(true);
		}
	}
}

void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container) {
	if (state == nullptr) {
		return;
	}

#if defined(OS_MACOSX) && OS_MACOSX == 1
	// On OS X, creation (and destruction) of the window, as well as its event
	// handling must happen on the main thread. Only drawing can be separate.
	caerVisualizerHandleEvents(state);
#endif

	if (container == nullptr) {
		return;
	}

	// Keep statistics up-to-date with all events, always.
	CAER_EVENT_PACKET_CONTAINER_ITERATOR_START(container)
			caerStatisticsStringUpdate(caerEventPacketContainerIteratorElement, &state->packetStatistics);
		CAER_EVENT_PACKET_CONTAINER_ITERATOR_END

		// Only render every Nth container (or packet, if using standard visualizer).
	state->packetSubsampleCount++;

	if (state->packetSubsampleCount >= state->packetSubsampleRendering.load(std::memory_order_relaxed)) {
		state->packetSubsampleCount = 0;
	}
	else {
		return;
	}

	caerEventPacketContainer containerCopy = caerEventPacketContainerCopyAllEvents(container);
	if (containerCopy == nullptr) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to copy event packet container for rendering.");

		return;
	}

	if (!ringBufferPut(state->dataTransfer, containerCopy)) {
		caerEventPacketContainerFree(containerCopy);

		caerModuleLog(state->parentModule, CAER_LOG_INFO,
			"Visualizer: Failed to move event packet container copy to ring-buffer (full).");
		return;
	}
}

void caerVisualizerExit(caerVisualizerState state) {
	if (state == nullptr) {
		return;
	}

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(state->parentModule->moduleNode, state, &caerVisualizerConfigListener);

	// Shut down rendering thread and wait on it to finish.
	state->running.store(false);

	try {
		state->renderingThread.join();
	}
	catch (const std::system_error &) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Visualizer: Failed to join rendering thread. Error: %d.",
		errno);
	}

#if defined(OS_MACOSX) && OS_MACOSX == 1
	// On OS X, creation (and destruction) of the window, as well as its event
	// handling must happen on the main thread. Only drawing can be separate.
	caerVisualizerExitGraphics(state);
#endif

	// Now clean up the ring-buffer and its contents.
	caerEventPacketContainer container;
	while ((container = (caerEventPacketContainer) ringBufferGet(state->dataTransfer)) != nullptr) {
		caerEventPacketContainerFree(container);
	}

	ringBufferFree(state->dataTransfer);

	// Then the statistics string.
	caerStatisticsStringExit(&state->packetStatistics);

	caerModuleLog(state->parentModule, CAER_LOG_DEBUG, "Visualizer: Exited successfully.");

	// And finally the state memory.
	free(state);
}

void caerVisualizerReset(caerVisualizerState state) {
	if (state == nullptr) {
		return;
	}

	// Reset statistics and counters.
	caerStatisticsStringReset(&state->packetStatistics);
	state->packetSubsampleCount = 0;
}

static bool caerVisualizerInitGraphics(caerVisualizerState state) {
	// Set thread name to SFMLGraphics, so that the internal SFML
	// threads do get a generic, recognizable name, if any are
	// created when initializing the graphics sub-system.
	// TODO: thrd_set_name("SFMLGraphics");

	// Create display window and set its title.
	state->renderWindow = new sf::RenderWindow(sf::VideoMode(state->renderSizeX, state->renderSizeY),
		state->parentModule->moduleSubSystemString, sf::Style::Titlebar | sf::Style::Close);
	if (state->renderWindow == nullptr) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to create display window with sizeX=%" PRIu32 ", sizeY=%" PRIu32 ".",
			state->renderSizeX, state->renderSizeY);
		return (false);
	}

	// Enable VSync to avoid tearing.
	// TODO: state->renderWindow->setVerticalSyncEnabled(true);

	// Set frame rate limit to avoid too many refreshes.
	// TODO: state->renderWindow->setFramerateLimit(VISUALIZER_REFRESH_RATE);

	// Set scale transform for display window, update sizes.
	updateDisplaySize(state);

	// Set window position.
	updateDisplayLocation(state);

	// Initialize window to all black.
	state->renderWindow->clear(sf::Color::Black);
	state->renderWindow->display();

	// Re-load font here so it's hardware accelerated.
	// A display must have been created and used as target for this to work.
	state->font = new sf::Font();
	if (state->font == nullptr) {
		caerModuleLog(state->parentModule, CAER_LOG_WARNING,
			"Visualizer: Failed to create display font. Text rendering will not be possible.");
	}
	else {
		if (!state->font->loadFromMemory(LiberationSans_Bold_ttf, LiberationSans_Bold_ttf_len)) {
			caerModuleLog(state->parentModule, CAER_LOG_WARNING,
				"Visualizer: Failed to load display font. Text rendering will not be possible.");

			delete state->font;
			state->font = nullptr;
		}
	}

	return (true);
}

static void caerVisualizerHandleEvents(caerVisualizerState state) {
	sf::Event event;

	while (state->renderWindow->pollEvent(event)) {
		if (event.type == sf::Event::Closed) {
			sshsNodePutBool(state->parentModule->moduleNode, "running", false);
		}
		else if (event.type == sf::Event::KeyPressed || event.type == sf::Event::KeyReleased
			|| event.type == sf::Event::TextEntered) {
			// React to key presses, but only if they came from the corresponding display.
			if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::Up) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor += 0.5f;

				// Clip zoom factor.
				if (currentZoomFactor > 50) {
					currentZoomFactor = 50;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::Down) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor -= 0.5f;

				// Clip zoom factor.
				if (currentZoomFactor < 0.5f) {
					currentZoomFactor = 0.5f;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::W) {
				int32_t currentSubsampling = sshsNodeGetInt(state->parentModule->moduleNode, "subsampleRendering");

				currentSubsampling--;

				// Clip subsampling factor.
				if (currentSubsampling < 1) {
					currentSubsampling = 1;
				}

				sshsNodePutInt(state->parentModule->moduleNode, "subsampleRendering", currentSubsampling);
			}
			else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::E) {
				int32_t currentSubsampling = sshsNodeGetInt(state->parentModule->moduleNode, "subsampleRendering");

				currentSubsampling++;

				// Clip subsampling factor.
				if (currentSubsampling > 100000) {
					currentSubsampling = 100000;
				}

				sshsNodePutInt(state->parentModule->moduleNode, "subsampleRendering", currentSubsampling);
			}
			else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::Q) {
				bool currentShowStatistics = sshsNodeGetBool(state->parentModule->moduleNode, "showStatistics");

				sshsNodePutBool(state->parentModule->moduleNode, "showStatistics", !currentShowStatistics);
			}
			else {
				// Forward event to user-defined event handler.
				if (state->eventHandler != nullptr) {
					(*state->eventHandler)((caerVisualizerPublicState) state, event);
				}
			}
		}
		else if (event.type == sf::Event::MouseButtonPressed || event.type == sf::Event::MouseButtonReleased
			|| event.type == sf::Event::MouseWheelScrolled || event.type == sf::Event::MouseEntered
			|| event.type == sf::Event::MouseLeft || event.type == sf::Event::MouseMoved) {
			if (event.type == sf::Event::MouseWheelScrolled && event.mouseWheelScroll.delta > 0) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor += (0.1f * (float) event.mouseWheelScroll.delta);

				// Clip zoom factor.
				if (currentZoomFactor > 50) {
					currentZoomFactor = 50;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (event.type == sf::Event::MouseWheelScrolled && event.mouseWheelScroll.delta < 0) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				// Plus because dz is negative, so - and - is +.
				currentZoomFactor += (0.1f * (float) event.mouseWheelScroll.delta);

				// Clip zoom factor.
				if (currentZoomFactor < 0.5f) {
					currentZoomFactor = 0.5f;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else {
				// Forward event to user-defined event handler.
				if (state->eventHandler != nullptr) {
					(*state->eventHandler)((caerVisualizerPublicState) state, event);
				}
			}
		}
	}
}

static void caerVisualizerUpdateScreen(caerVisualizerState state) {
	caerEventPacketContainer container = (caerEventPacketContainer) ringBufferGet(state->dataTransfer);

	repeat: if (container != nullptr) {
		// Are there others? Only render last one, to avoid getting backed up!
		caerEventPacketContainer container2 = (caerEventPacketContainer) ringBufferGet(state->dataTransfer);

		if (container2 != nullptr) {
			caerEventPacketContainerFree(container);
			container = container2;
			goto repeat;
		}
	}

	bool drewSomething = false;

	if (container != nullptr) {
		// Update render window with new content. (0, 0) is upper left corner.
		// NULL renderer is supported and simply does nothing (black screen).
		if (state->renderer != nullptr) {
			drewSomething = (*state->renderer)((caerVisualizerPublicState) state, container);
		}

		// Free packet container copy.
		caerEventPacketContainerFree(container);
	}

	// Handle display resize (zoom and statistics).
	if (state->windowResize.load(std::memory_order_relaxed)) {
		state->windowResize.store(false);

		// Update statistics flag and resize display appropriately.
		updateDisplaySize(state);
	}

	// Handle display move.
	if (state->windowMove.load(std::memory_order_relaxed)) {
		state->windowMove.store(false);

		// Move display location appropriately.
		updateDisplayLocation(state);
	}

	// Render content to display.
	if (drewSomething) {
		// Render statistics string.
		bool doStatistics = (state->showStatistics && state->font != nullptr);

		if (doStatistics) {
			// Split statistics string in two to use less horizontal space.
			// Put it below the normal render region, so people can access from
			// (0,0) to (x-1,y-1) normally without fear of overwriting statistics.
			sf::Text totalEventsText(state->packetStatistics.currentStatisticsStringTotal, *state->font,
			GLOBAL_FONT_SIZE);
			totalEventsText.setFillColor(sf::Color::White);
			totalEventsText.setPosition(GLOBAL_FONT_SPACING, state->renderSizeY + GLOBAL_FONT_SPACING);
			state->renderWindow->draw(totalEventsText);

			sf::Text validEventsText(state->packetStatistics.currentStatisticsStringValid, *state->font,
			GLOBAL_FONT_SIZE);
			validEventsText.setFillColor(sf::Color::White);
			validEventsText.setPosition(GLOBAL_FONT_SPACING,
				state->renderSizeY + (2 * GLOBAL_FONT_SPACING) + GLOBAL_FONT_SIZE);
			state->renderWindow->draw(validEventsText);
		}

		// Draw to screen.
		state->renderWindow->display();

		// Reset window to all black for next rendering pass.
		state->renderWindow->clear(sf::Color::Black);
	}
}

static void caerVisualizerExitGraphics(caerVisualizerState state) {
	// Update visualizer location
	saveDisplayLocation(state);

	// Close rendering window and free memory.
	state->renderWindow->close();

	delete state->font;
	delete state->renderWindow;
}

static int caerVisualizerRenderThread(void *visualizerState) {
	if (visualizerState == nullptr) {
		return (thrd_error);
	}

	caerVisualizerState state = (caerVisualizerState) visualizerState;

	// Set thread name.
	thrd_set_name(state->parentModule->moduleSubSystemString);

#if defined(OS_MACOSX) && OS_MACOSX == 1
	// On OS X, creation (and destruction) of the window, as well as its event
	// handling must happen on the main thread. Only drawing can be separate.
	while (state->running.load(std::memory_order_relaxed)) {
		caerVisualizerUpdateScreen(state);
	}
#else
	if (!caerVisualizerInitGraphics(state)) {
		return (thrd_error);
	}

	while (state->running.load(std::memory_order_relaxed)) {
		caerVisualizerHandleEvents(state);
		caerVisualizerUpdateScreen(state);
	}

	caerVisualizerExitGraphics(state);
#endif

	return (thrd_success);
}

// InitSize is deferred and called from Run, because we need actual packets.
static bool caerVisualizerModuleInit(caerModuleData moduleData);
static bool caerVisualizerModuleInitSize(caerModuleData moduleData, int16_t *inputs, size_t inputsSize);
static void caerVisualizerModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerVisualizerModuleExit(caerModuleData moduleData);
static void caerVisualizerModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions VisualizerFunctions = { .moduleInit = &caerVisualizerModuleInit, .moduleRun =
	&caerVisualizerModuleRun, .moduleConfig = nullptr, .moduleExit = &caerVisualizerModuleExit, .moduleReset =
	&caerVisualizerModuleReset };

static const struct caer_event_stream_in VisualizerInputs[] = { { .type = -1, .number = -1, .readOnly = true } };

static const struct caer_module_info VisualizerInfo = { .version = 1, .name = "Visualizer", .description =
	"Visualize data in various forms.", .type = CAER_MODULE_OUTPUT, .memSize = 0, .functions = &VisualizerFunctions,
	.inputStreams = VisualizerInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(VisualizerInputs), .outputStreams =
		nullptr, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&VisualizerInfo);
}

static bool caerVisualizerModuleInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	size_t inputsSize;
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, &inputsSize);
	if (inputs == nullptr) {
		return (false);
	}

	sshsNodeCreateString(moduleData->moduleNode, "renderer", "Polarity", 0, 100, SSHS_FLAGS_NORMAL,
		"Renderer to use to generate content.");
	sshsNodeRemoveAttribute(moduleData->moduleNode, "rendererListOptions", SSHS_STRING);
	sshsNodeCreateString(moduleData->moduleNode, "rendererListOptions", caerVisualizerRendererListOptionsString, 0, 200,
		SSHS_FLAGS_READ_ONLY, "List of available renderers.");
	sshsNodeCreateString(moduleData->moduleNode, "eventHandler", "None", 0, 100, SSHS_FLAGS_NORMAL,
		"Event handlers to handle mouse and keyboard events.");
	sshsNodeRemoveAttribute(moduleData->moduleNode, "eventHandlerListOptions", SSHS_STRING);
	sshsNodeCreateString(moduleData->moduleNode, "eventHandlerListOptions", caerVisualizerHandlerListOptionsString, 0,
		200, SSHS_FLAGS_READ_ONLY, "List of available event handlers.");

	// Initialize visualizer. Needs information from a packet (the source ID)!
	if (!caerVisualizerModuleInitSize(moduleData, inputs, inputsSize)) {
		return (false);
	}

	return (true);
}

static bool caerVisualizerModuleInitSize(caerModuleData moduleData, int16_t *inputs, size_t inputsSize) {
	// Default sizes if nothing else is specified in sourceInfo node.
	int16_t sizeX = 20;
	int16_t sizeY = 20;
	int16_t sourceID = -1;

	// Search for biggest sizes amongst all event packets.
	for (size_t i = 0; i < inputsSize; i++) {
		// Get size information from source.
		sourceID = inputs[i];

		sshsNode sourceInfoNode = caerMainloopGetSourceInfo(sourceID);
		if (sourceInfoNode == nullptr) {
			return (false);
		}

		// Default sizes if nothing else is specified in sourceInfo node.
		int16_t packetSizeX = 0;
		int16_t packetSizeY = 0;

		// Get sizes from sourceInfo node. visualizer prefix takes precedence,
		// for APS and DVS images, alternative prefixes are provided, as well
		// as for generic data visualization.
		if (sshsNodeAttributeExists(sourceInfoNode, "visualizerSizeX", SSHS_SHORT)) {
			packetSizeX = sshsNodeGetShort(sourceInfoNode, "visualizerSizeX");
			packetSizeY = sshsNodeGetShort(sourceInfoNode, "visualizerSizeY");
		}
		else if (sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) {
			packetSizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
			packetSizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");
		}

		if (packetSizeX > sizeX) {
			sizeX = packetSizeX;
		}

		if (packetSizeY > sizeY) {
			sizeY = packetSizeY;
		}
	}

	// Search for renderer in list.
	caerVisualizerRenderer renderer = nullptr;

	char *rendererChoice = sshsNodeGetString(moduleData->moduleNode, "renderer");

	for (size_t i = 0; i < (sizeof(caerVisualizerRendererList) / sizeof(struct caer_visualizer_renderers)); i++) {
		if (strcmp(rendererChoice, caerVisualizerRendererList[i].name) == 0) {
			renderer = caerVisualizerRendererList[i].renderer;
			break;
		}
	}

	free(rendererChoice);

	// Search for event handler in list.
	caerVisualizerEventHandler eventHandler = nullptr;

	char *eventHandlerChoice = sshsNodeGetString(moduleData->moduleNode, "eventHandler");

	for (size_t i = 0; i < (sizeof(caerVisualizerHandlerList) / sizeof(struct caer_visualizer_handlers)); i++) {
		if (strcmp(eventHandlerChoice, caerVisualizerHandlerList[i].name) == 0) {
			eventHandler = caerVisualizerHandlerList[i].handler;
			break;
		}
	}

	free(eventHandlerChoice);

	moduleData->moduleState = caerVisualizerInit(renderer, eventHandler, U32T(sizeX), U32T(sizeY),
	VISUALIZER_DEFAULT_ZOOM, true, moduleData, sourceID);
	if (moduleData->moduleState == nullptr) {
		return (false);
	}

	return (true);
}

static void caerVisualizerModuleExit(caerModuleData moduleData) {
	// Shut down rendering.
	caerVisualizerExit((caerVisualizerState) moduleData->moduleState);
	moduleData->moduleState = nullptr;
}

static void caerVisualizerModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	// Reset counters for statistics on reset.
	caerVisualizerReset((caerVisualizerState) moduleData->moduleState);
}

static void caerVisualizerModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	// Without a packet container with events, we cannot initialize or render anything.
	if (in == nullptr || caerEventPacketContainerGetEventsNumber(in) == 0) {
		return;
	}

	// Render given packet container.
	caerVisualizerUpdate((caerVisualizerState) moduleData->moduleState, in);
}
