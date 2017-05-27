#include "visualizer.h"
#include "visualizer_renderers.h"
#include "visualizer_handlers.h"
#include "base/mainloop.h"
#include "ext/ringbuffer/ringbuffer.h"
#include "modules/statistics/statistics.h"
#ifdef HAVE_PTHREADS
#include "ext/c11threads_posix.h"
#endif

#include <math.h>
#include <stdatomic.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_ttf.h>

static once_flag visualizerSystemIsInitialized = ONCE_FLAG_INIT;

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

static struct caer_visualizer_handlers caerVisualizerHandlerList[] = { { "None", NULL }, { "Spikes",
	&caerVisualizerEventHandlerSpikeEvents }, { "Input", &caerInputVisualizerEventHandler } };

struct caer_visualizer_state {
	void *eventSourceModuleState;
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	int32_t bitmapRendererSizeX;
	int32_t bitmapRendererSizeY;
	ALLEGRO_FONT *displayFont;
	atomic_bool running;
	atomic_bool displayWindowResize;
	int32_t displayWindowSizeX;
	int32_t displayWindowSizeY;
	ALLEGRO_DISPLAY *displayWindow;
	ALLEGRO_EVENT_QUEUE *displayEventQueue;
	ALLEGRO_TIMER *displayTimer;
	ALLEGRO_BITMAP *bitmapRenderer;
	bool bitmapDrawUpdate;
	RingBuffer dataTransfer;
	thrd_t renderingThread;
	caerVisualizerRenderer renderer;
	caerVisualizerEventHandler eventHandler;
	caerModuleData parentModule;
	bool showStatistics;
	struct caer_statistics_state packetStatistics;
	atomic_int_fast32_t packetSubsampleRendering;
	int32_t packetSubsampleCount;
};

static void updateDisplaySize(caerVisualizerState state, bool updateTransform);
static void updateDisplayLocation(caerVisualizerState state);
static void saveDisplayLocation(caerVisualizerState state);
static void caerVisualizerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static bool caerVisualizerInitGraphics(caerVisualizerState state);
static void caerVisualizerUpdateScreen(caerVisualizerState state);
static void caerVisualizerExitGraphics(caerVisualizerState state);
static int caerVisualizerRenderThread(void *visualizerState);

#define xstr(a) str(a)
#define str(a) #a

#ifdef CM_SHARE_DIR
#define CM_SHARE_DIRECTORY xstr(CM_SHARE_DIR)
#else
#define CM_SHARE_DIRECTORY "/usr/share/caer"
#endif

#ifdef CM_BUILD_DIR
#define CM_BUILD_DIRECTORY xstr(CM_BUILD_DIR)
#else
#define CM_BUILD_DIRECTORY ""
#endif

#define GLOBAL_RESOURCES_DIRECTORY "ext/resources"
#define GLOBAL_FONT_NAME "LiberationSans-Bold.ttf"
#define GLOBAL_FONT_SIZE 20 // in pixels
#define GLOBAL_FONT_SPACING 5 // in pixels

// Calculated at system init.
static int STATISTICS_WIDTH = 0;
static int STATISTICS_HEIGHT = 0;

static const char *systemFont = CM_SHARE_DIRECTORY "/" GLOBAL_FONT_NAME;
static const char *buildFont = CM_BUILD_DIRECTORY "/" GLOBAL_RESOURCES_DIRECTORY "/" GLOBAL_FONT_NAME;
static const char *globalFontPath = NULL;

void caerVisualizerSystemInit(void) {
	// Remember original thread name.
	char originalThreadName[15 + 1]; // +1 for terminating NUL character.
	thrd_get_name(originalThreadName, 15);
	originalThreadName[15] = '\0';

	// Set custom thread name for Allegro system init.
	thrd_set_name("AllegroSysInit");

	// Initialize the Allegro library.
	if (al_init()) {
		// Successfully initialized Allegro.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro library initialized successfully.");
	}
	else {
		// Failed to initialize Allegro.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro library.");
		exit(EXIT_FAILURE);
	}

	// Set correct names.
	al_set_org_name("iniLabs");
	al_set_app_name("cAER");

	// Search for global font, first in system share dir, else in build dir.
	if (access(systemFont, R_OK) == 0) {
		globalFontPath = systemFont;
	}
	else {
		globalFontPath = buildFont;
	}

	// Now load addons: primitives to draw, fonts (and TTF) to write text.
	if (al_init_primitives_addon()) {
		// Successfully initialized Allegro primitives addon.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro primitives addon initialized successfully.");
	}
	else {
		// Failed to initialize Allegro primitives addon.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro primitives addon.");
		exit(EXIT_FAILURE);
	}

	al_init_font_addon();

	if (al_init_ttf_addon()) {
		// Successfully initialized Allegro TTF addon.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro TTF addon initialized successfully.");
	}
	else {
		// Failed to initialize Allegro TTF addon.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro TTF addon.");
		exit(EXIT_FAILURE);
	}

	// Determine biggest possible statistics string.
	size_t maxStatStringLength = (size_t) snprintf(NULL, 0, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);

	char maxStatString[maxStatStringLength + 1];
	snprintf(maxStatString, maxStatStringLength + 1, CAER_STATISTICS_STRING_TOTAL, UINT64_MAX);
	maxStatString[maxStatStringLength] = '\0';

	// Load statistics font into memory.
	ALLEGRO_FONT *font = al_load_font(globalFontPath, GLOBAL_FONT_SIZE, 0);
	if (font == NULL) {
		caerLog(CAER_LOG_ERROR, "Visualizer", "Failed to load display font '%s'.", globalFontPath);
	}

	// Determine statistics string width.
	if (font != NULL) {
		STATISTICS_WIDTH = (2 * GLOBAL_FONT_SPACING) + al_get_text_width(font, maxStatString);

		STATISTICS_HEIGHT = (3 * GLOBAL_FONT_SPACING) + (2 * GLOBAL_FONT_SIZE);

		al_destroy_font(font);
	}

	// Install main event sources: mouse and keyboard.
	if (al_install_mouse()) {
		// Successfully initialized Allegro mouse event source.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro mouse event source initialized successfully.");
	}
	else {
		// Failed to initialize Allegro mouse event source.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro mouse event source.");
	}

	if (al_install_keyboard()) {
		// Successfully initialized Allegro keyboard event source.
		caerLog(CAER_LOG_DEBUG, "Visualizer", "Allegro keyboard event source initialized successfully.");
	}
	else {
		// Failed to initialize Allegro keyboard event source.
		caerLog(CAER_LOG_EMERGENCY, "Visualizer", "Failed to initialize Allegro keyboard event source.");
	}

	// On success, set thread name back to original. Any threads created by Allegro
	// will have their own, unique name (AllegroSysInit) from above.
	thrd_set_name(originalThreadName);
}

caerVisualizerState caerVisualizerInit(caerVisualizerRenderer renderer, caerVisualizerEventHandler eventHandler,
	int32_t bitmapSizeX, int32_t bitmapSizeY, float defaultZoomFactor, bool defaultShowStatistics,
	caerModuleData parentModule, int16_t eventSourceID) {
	// Initialize visualizer framework (load fonts etc.). Do only once per startup!
	call_once(&visualizerSystemIsInitialized, &caerVisualizerSystemInit);

	// Allocate memory for visualizer state.
	caerVisualizerState state = calloc(1, sizeof(struct caer_visualizer_state));
	if (state == NULL) {
		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to allocate state memory.");
		return (NULL);
	}

	state->parentModule = parentModule;
	state->visualizerConfigNode = parentModule->moduleNode;
	if (eventSourceID >= 0) {
		state->eventSourceModuleState = caerMainloopGetSourceState(eventSourceID);
		state->eventSourceConfigNode = caerMainloopGetSourceNode(eventSourceID);
	}

	// Configuration.
	sshsNodeCreateInt(parentModule->moduleNode, "subsampleRendering", 1, 1, 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Speed-up rendering by only taking every Nth EventPacketContainer to render.");
	sshsNodeCreateBool(parentModule->moduleNode, "showStatistics", defaultShowStatistics, SSHS_FLAGS_NORMAL,
		"Show event statistics above content (top of window).");
	sshsNodeCreateFloat(parentModule->moduleNode, "zoomFactor", defaultZoomFactor, 0.5f, 50.0f, SSHS_FLAGS_NORMAL,
		"Content zoom factor.");
	sshsNodeCreateInt(parentModule->moduleNode, "windowPositionX", VISUALIZER_DEFAULT_POSITION_X, 0, INT32_MAX,
		SSHS_FLAGS_NORMAL, "Position of window on screen (X coordinate).");
	sshsNodeCreateInt(parentModule->moduleNode, "windowPositionY", VISUALIZER_DEFAULT_POSITION_Y, 0, INT32_MAX,
		SSHS_FLAGS_NORMAL, "Position of window on screen (Y coordinate).");

	atomic_store(&state->packetSubsampleRendering, sshsNodeGetInt(parentModule->moduleNode, "subsampleRendering"));

	// Remember sizes.
	state->bitmapRendererSizeX = bitmapSizeX;
	state->bitmapRendererSizeY = bitmapSizeY;

	updateDisplaySize(state, false);

	// Remember rendering and event handling function.
	state->renderer = renderer;
	state->eventHandler = eventHandler;

	// Enable packet statistics.
	if (!caerStatisticsStringInit(&state->packetStatistics)) {
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to initialize statistics string.");
		return (NULL);
	}

	// Initialize ring-buffer to transfer data to render thread.
	state->dataTransfer = ringBufferInit(64);
	if (state->dataTransfer == NULL) {
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to initialize ring-buffer.");
		return (NULL);
	}

	// Start separate rendering thread. Decouples presentation from
	// data processing and preparation. Communication over ring-buffer.
	atomic_store(&state->running, true);

	if (thrd_create(&state->renderingThread, &caerVisualizerRenderThread, state) != thrd_success) {
		ringBufferFree(state->dataTransfer);
		caerStatisticsStringExit(&state->packetStatistics);
		free(state);

		caerModuleLog(parentModule, CAER_LOG_ERROR, "Visualizer: Failed to start rendering thread.");
		return (NULL);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(parentModule->moduleNode, state, &caerVisualizerConfigListener);

	caerModuleLog(parentModule, CAER_LOG_DEBUG, "Visualizer: Initialized successfully.");

	return (state);
}

static void updateDisplayLocation(caerVisualizerState state) {
	al_set_window_position(state->displayWindow, sshsNodeGetInt(state->parentModule->moduleNode, "windowPositionX"),
		sshsNodeGetInt(state->parentModule->moduleNode, "windowPositionY"));
}

static void saveDisplayLocation(caerVisualizerState state) {
	int xWinPos = 0, yWinPos = 0;

	al_get_window_position(state->displayWindow, &xWinPos, &yWinPos);

	// update parent module value
	sshsNodePutInt(state->parentModule->moduleNode, "windowPositionX", xWinPos);
	sshsNodePutInt(state->parentModule->moduleNode, "windowPositionY", yWinPos);
}

static void updateDisplaySize(caerVisualizerState state, bool updateTransform) {
	state->showStatistics = sshsNodeGetBool(state->parentModule->moduleNode, "showStatistics");
	float zoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

	int32_t displayWindowSizeX = state->bitmapRendererSizeX;
	int32_t displayWindowSizeY = state->bitmapRendererSizeY;

	// When statistics are turned on, we need to add some space to the
	// X axis for displaying the whole line and the Y axis for spacing.
	if (state->showStatistics) {
		if (STATISTICS_WIDTH > displayWindowSizeX) {
			displayWindowSizeX = STATISTICS_WIDTH;
		}

		displayWindowSizeY += STATISTICS_HEIGHT;
	}

	state->displayWindowSizeX = I32T((float ) displayWindowSizeX * zoomFactor);
	state->displayWindowSizeY = I32T((float ) displayWindowSizeY * zoomFactor);

	// Update Allegro drawing transformation to implement scaling.
	if (updateTransform) {
		al_set_target_backbuffer(state->displayWindow);

		ALLEGRO_TRANSFORM t;
		al_identity_transform(&t);
		al_scale_transform(&t, zoomFactor, zoomFactor);
		al_use_transform(&t);

		al_resize_display(state->displayWindow, state->displayWindowSizeX, state->displayWindowSizeY);
	}
}

static void caerVisualizerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerVisualizerState state = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_FLOAT && caerStrEquals(changeKey, "zoomFactor")) {
			// Set resize flag.
			atomic_store(&state->displayWindowResize, true);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "showStatistics")) {
			// Set resize flag. This will then also update the showStatistics flag, ensuring
			// statistics are never shown without the screen having been properly resized first.
			atomic_store(&state->displayWindowResize, true);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "subsampleRendering")) {
			atomic_store(&state->packetSubsampleRendering, changeValue.iint);
		}
	}
}

void caerVisualizerUpdate(caerVisualizerState state, caerEventPacketContainer container) {
	if (state == NULL || container == NULL) {
		return;
	}

	// Keep statistics up-to-date with all events, always.
	CAER_EVENT_PACKET_CONTAINER_ITERATOR_START(container)
			caerStatisticsStringUpdate(caerEventPacketContainerIteratorElement, &state->packetStatistics);
		CAER_EVENT_PACKET_CONTAINER_ITERATOR_END

		// Only render every Nth container (or packet, if using standard visualizer).
	state->packetSubsampleCount++;

	if (state->packetSubsampleCount >= atomic_load_explicit(&state->packetSubsampleRendering, memory_order_relaxed)) {
		state->packetSubsampleCount = 0;
	}
	else {
		return;
	}

	caerEventPacketContainer containerCopy = caerEventPacketContainerCopyAllEvents(container);
	if (containerCopy == NULL) {
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
	if (state == NULL) {
		return;
	}

	// Update visualizer location
	saveDisplayLocation(state);

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(state->parentModule->moduleNode, state, &caerVisualizerConfigListener);

	// Shut down rendering thread and wait on it to finish.
	atomic_store(&state->running, false);

	if ((errno = thrd_join(state->renderingThread, NULL)) != thrd_success) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Visualizer: Failed to join rendering thread. Error: %d.",
		errno);
	}

	// Now clean up the ring-buffer and its contents.
	caerEventPacketContainer container;
	while ((container = ringBufferGet(state->dataTransfer)) != NULL) {
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
	if (state == NULL) {
		return;
	}

	// Reset statistics and counters.
	caerStatisticsStringReset(&state->packetStatistics);
	state->packetSubsampleCount = 0;
}

static bool caerVisualizerInitGraphics(caerVisualizerState state) {
	// Create display window.
	state->displayWindow = al_create_display(state->displayWindowSizeX, state->displayWindowSizeY);
	if (state->displayWindow == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to create display window with sizeX=%d, sizeY=%d.", state->displayWindowSizeX,
			state->displayWindowSizeY);
		return (false);
	}

	// Set display window name.
	al_set_window_title(state->displayWindow, state->parentModule->moduleSubSystemString);

	// Initialize window to all black.
	al_set_target_backbuffer(state->displayWindow);
	al_clear_to_color(al_map_rgb(0, 0, 0));
	al_flip_display();

	// Set scale transform for display window, update sizes.
	updateDisplaySize(state, true);

	// Set window position.
	updateDisplayLocation(state);

	// Create memory bitmap for drawing into.
	al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP | ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);
	state->bitmapRenderer = al_create_bitmap(state->bitmapRendererSizeX, state->bitmapRendererSizeY);

	if (state->bitmapRenderer == NULL) {
		// Clean up all memory that may have been used.
		caerVisualizerExitGraphics(state);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Visualizer: Failed to create bitmap element with sizeX=%d, sizeY=%d.", state->bitmapRendererSizeX,
			state->bitmapRendererSizeY);
		return (false);
	}

	// Clear bitmap to all black.
	al_set_target_bitmap(state->bitmapRenderer);
	al_clear_to_color(al_map_rgb(0, 0, 0));

	// Timers and event queues for the rendering side.
	state->displayEventQueue = al_create_event_queue();
	if (state->displayEventQueue == NULL) {
		// Clean up all memory that may have been used.
		caerVisualizerExitGraphics(state);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Visualizer: Failed to create event queue.");
		return (false);
	}

	state->displayTimer = al_create_timer((double) (1.00f / VISUALIZER_REFRESH_RATE));
	if (state->displayTimer == NULL) {
		// Clean up all memory that may have been used.
		caerVisualizerExitGraphics(state);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Visualizer: Failed to create timer.");
		return (false);
	}

	al_register_event_source(state->displayEventQueue, al_get_display_event_source(state->displayWindow));
	al_register_event_source(state->displayEventQueue, al_get_timer_event_source(state->displayTimer));
	al_register_event_source(state->displayEventQueue, al_get_keyboard_event_source());
	al_register_event_source(state->displayEventQueue, al_get_mouse_event_source());

	// Re-load font here so it's hardware accelerated.
	// A display must have been created and used as target for this to work.
	state->displayFont = al_load_font(globalFontPath, GLOBAL_FONT_SIZE, 0);
	if (state->displayFont == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_WARNING,
			"Visualizer: Failed to load display font '%s'. Text rendering will not be possible.", globalFontPath);
	}

	// Everything fine, start timer for refresh.
	al_start_timer(state->displayTimer);

	return (true);
}

static void caerVisualizerUpdateScreen(caerVisualizerState state) {
	caerEventPacketContainer container = ringBufferGet(state->dataTransfer);

	repeat: if (container != NULL) {
		// Are there others? Only render last one, to avoid getting backed up!
		caerEventPacketContainer container2 = ringBufferGet(state->dataTransfer);

		if (container2 != NULL) {
			caerEventPacketContainerFree(container);
			container = container2;
			goto repeat;
		}
	}

	if (container != NULL) {
		al_set_target_bitmap(state->bitmapRenderer);

		// Update bitmap with new content. (0, 0) is upper left corner.
		// NULL renderer is supported and simply does nothing (black screen).
		if (state->renderer != NULL) {
			bool didDrawSomething = (*state->renderer)((caerVisualizerPublicState) state, container,
				!state->bitmapDrawUpdate);

			// Remember if something was drawn, even just once.
			if (!state->bitmapDrawUpdate) {
				state->bitmapDrawUpdate = didDrawSomething;
			}
		}

		// Free packet container copy.
		caerEventPacketContainerFree(container);
	}

	bool redraw = false;
	ALLEGRO_EVENT displayEvent;

	handleEvents: al_wait_for_event(state->displayEventQueue, &displayEvent);

	if (displayEvent.type == ALLEGRO_EVENT_TIMER) {
		redraw = true;
	}
	else if (displayEvent.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
		sshsNodePutBool(state->parentModule->moduleNode, "running", false);
	}
	else if (displayEvent.type == ALLEGRO_EVENT_KEY_CHAR || displayEvent.type == ALLEGRO_EVENT_KEY_DOWN
		|| displayEvent.type == ALLEGRO_EVENT_KEY_UP) {
		// React to key presses, but only if they came from the corresponding display.
		if (displayEvent.keyboard.display == state->displayWindow) {
			if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_UP) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor += 0.5f;

				// Clip zoom factor.
				if (currentZoomFactor > 50) {
					currentZoomFactor = 50;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_DOWN) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor -= 0.5f;

				// Clip zoom factor.
				if (currentZoomFactor < 0.5f) {
					currentZoomFactor = 0.5f;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_W) {
				int32_t currentSubsampling = sshsNodeGetInt(state->parentModule->moduleNode, "subsampleRendering");

				currentSubsampling--;

				// Clip subsampling factor.
				if (currentSubsampling < 1) {
					currentSubsampling = 1;
				}

				sshsNodePutInt(state->parentModule->moduleNode, "subsampleRendering", currentSubsampling);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_E) {
				int32_t currentSubsampling = sshsNodeGetInt(state->parentModule->moduleNode, "subsampleRendering");

				currentSubsampling++;

				// Clip subsampling factor.
				if (currentSubsampling > 100000) {
					currentSubsampling = 100000;
				}

				sshsNodePutInt(state->parentModule->moduleNode, "subsampleRendering", currentSubsampling);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_KEY_DOWN && displayEvent.keyboard.keycode == ALLEGRO_KEY_Q) {
				bool currentShowStatistics = sshsNodeGetBool(state->parentModule->moduleNode, "showStatistics");

				sshsNodePutBool(state->parentModule->moduleNode, "showStatistics", !currentShowStatistics);
			}
			else {
				// Forward event to user-defined event handler.
				if (state->eventHandler != NULL) {
					(*state->eventHandler)((caerVisualizerPublicState) state, displayEvent);
				}
			}
		}
	}
	else if (displayEvent.type == ALLEGRO_EVENT_MOUSE_AXES || displayEvent.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN
		|| displayEvent.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP || displayEvent.type == ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY
		|| displayEvent.type == ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY || displayEvent.type == ALLEGRO_EVENT_MOUSE_WARPED) {
		// React to mouse movements, but only if they came from the corresponding display.
		if (displayEvent.mouse.display == state->displayWindow) {
			if (displayEvent.type == ALLEGRO_EVENT_MOUSE_AXES && displayEvent.mouse.dz > 0) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				currentZoomFactor += (0.1f * (float) displayEvent.mouse.dz);

				// Clip zoom factor.
				if (currentZoomFactor > 50) {
					currentZoomFactor = 50;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else if (displayEvent.type == ALLEGRO_EVENT_MOUSE_AXES && displayEvent.mouse.dz < 0) {
				float currentZoomFactor = sshsNodeGetFloat(state->parentModule->moduleNode, "zoomFactor");

				// Plus because dz is negative, so - and - is +.
				currentZoomFactor += (0.1f * (float) displayEvent.mouse.dz);

				// Clip zoom factor.
				if (currentZoomFactor < 0.5f) {
					currentZoomFactor = 0.5f;
				}

				sshsNodePutFloat(state->parentModule->moduleNode, "zoomFactor", currentZoomFactor);
			}
			else {
				// Forward event to user-defined event handler.
				if (state->eventHandler != NULL) {
					(*state->eventHandler)((caerVisualizerPublicState) state, displayEvent);
				}
			}
		}
	}

	if (!al_is_event_queue_empty(state->displayEventQueue)) {
		// Handle all events before rendering, to avoid
		// having them backed up too much.
		goto handleEvents;
	}

	// Handle display resize (zoom).
	if (atomic_load_explicit(&state->displayWindowResize, memory_order_relaxed)) {
		atomic_store(&state->displayWindowResize, false);

		// Update statistics flag and resize display appropriately.
		updateDisplaySize(state, true);
	}

	// Render content to display.
	if (redraw && state->bitmapDrawUpdate) {
		state->bitmapDrawUpdate = false;

		al_set_target_backbuffer(state->displayWindow);
		al_clear_to_color(al_map_rgb(0, 0, 0));

		// Render statistics string.
		bool doStatistics = (state->showStatistics && state->displayFont != NULL);

		if (doStatistics) {
			// Split statistics string in two to use less horizontal space.
			al_draw_text(state->displayFont, al_map_rgb(255, 255, 255), GLOBAL_FONT_SPACING,
			GLOBAL_FONT_SPACING, 0, state->packetStatistics.currentStatisticsStringTotal);

			al_draw_text(state->displayFont, al_map_rgb(255, 255, 255), GLOBAL_FONT_SPACING,
				(2 * GLOBAL_FONT_SPACING) + GLOBAL_FONT_SIZE, 0, state->packetStatistics.currentStatisticsStringValid);
		}

		// Blit bitmap to screen.
		al_draw_bitmap(state->bitmapRenderer, 0, (doStatistics) ? ((float) STATISTICS_HEIGHT) : (0), 0);

		al_flip_display();
	}
}

static void caerVisualizerExitGraphics(caerVisualizerState state) {
	al_set_target_bitmap(NULL);

	if (state->bitmapRenderer != NULL) {
		al_destroy_bitmap(state->bitmapRenderer);
		state->bitmapRenderer = NULL;
	}

	if (state->displayFont != NULL) {
		al_destroy_font(state->displayFont);
		state->displayFont = NULL;
	}

	// Destroy event queue first to ensure all sources get
	// unregistered before being destroyed in turn.
	if (state->displayEventQueue != NULL) {
		al_destroy_event_queue(state->displayEventQueue);
		state->displayEventQueue = NULL;
	}

	if (state->displayTimer != NULL) {
		al_destroy_timer(state->displayTimer);
		state->displayTimer = NULL;
	}

	if (state->displayWindow != NULL) {
		al_destroy_display(state->displayWindow);
		state->displayWindow = NULL;
	}

}

static int caerVisualizerRenderThread(void *visualizerState) {
	if (visualizerState == NULL) {
		return (thrd_error);
	}

	caerVisualizerState state = visualizerState;

	// Set thread name to AllegroGraphics, so that the internal Allegro
	// threads do get a generic, recognizable name, if any are
	// created when initializing the graphics sub-system.
	thrd_set_name("AllegroGraphics");

	if (!caerVisualizerInitGraphics(state)) {
		return (thrd_error);
	}

	// Set thread name.
	thrd_set_name(state->parentModule->moduleSubSystemString);

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		caerVisualizerUpdateScreen(state);
	}

	caerVisualizerExitGraphics(state);

	return (thrd_success);
}

// InitSize is deferred and called from Run, because we need actual packets.
static bool caerVisualizerModuleInit(caerModuleData moduleData);
static bool caerVisualizerModuleInitSize(caerModuleData moduleData, caerEventPacketContainer container);
static void caerVisualizerModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out);
static void caerVisualizerModuleExit(caerModuleData moduleData);
static void caerVisualizerModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions VisualizerFunctions = { .moduleInit = &caerVisualizerModuleInit, .moduleRun =
	&caerVisualizerModuleRun, .moduleConfig = NULL, .moduleExit = &caerVisualizerModuleExit, .moduleReset =
	&caerVisualizerModuleReset };

static const struct caer_event_stream_in VisualizerInputs[] = { { .type = -1, .number = -1, .readOnly = true } };

static const struct caer_module_info VisualizerInfo = { .version = 1, .name = "Visualizer", .type = CAER_MODULE_OUTPUT,
	.memSize = 0, .functions = &VisualizerFunctions, .inputStreams = VisualizerInputs, .inputStreamsSize =
		CAER_EVENT_STREAM_IN_SIZE(VisualizerInputs), .outputStreams = NULL, .outputStreamsSize = 0, };

caerModuleInfo caerModuleGetInfo(void) {
	return (&VisualizerInfo);
}

static bool caerVisualizerModuleInit(caerModuleData moduleData) {
	sshsNodeCreateString(moduleData->moduleNode, "renderer", "Polarity", 0, 100, SSHS_FLAGS_NORMAL,
		"Renderer to use to generate content.");
	sshsNodeCreateString(moduleData->moduleNode, "rendererListOptions", caerVisualizerRendererListOptionsString, 0, 200,
		SSHS_FLAGS_READ_ONLY_FORCE_DEFAULT_VALUE, "List of available renderers.");
	sshsNodeCreateString(moduleData->moduleNode, "eventHandler", "None", 0, 100, SSHS_FLAGS_NORMAL,
		"Event handlers to handle mouse and keyboard events.");
	sshsNodeCreateString(moduleData->moduleNode, "eventHandlerListOptions", caerVisualizerHandlerListOptionsString, 0,
		200, SSHS_FLAGS_READ_ONLY_FORCE_DEFAULT_VALUE, "List of available event handlers.");

	return (true);
}

static bool caerVisualizerModuleInitSize(caerModuleData moduleData, caerEventPacketContainer container) {
	// Default sizes if nothing else is specified in sourceInfo node.
	int16_t sizeX = 20;
	int16_t sizeY = 20;
	int16_t sourceID = -1;

	// Search for biggest sizes amongst all event packets.
	CAER_EVENT_PACKET_CONTAINER_ITERATOR_START(container)
		// Get size information from source.
		sourceID = caerEventPacketHeaderGetEventSource(caerEventPacketContainerIteratorElement);

		sshsNode sourceInfoNode = caerMainloopGetSourceInfo(sourceID);
		if (sourceInfoNode == NULL) {
			// This should never happen, but we handle it gracefully.
			caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to get source info to setup visualizer resolution.");
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
		else if (sshsNodeAttributeExists(sourceInfoNode, "dvsSizeX", SSHS_SHORT)
			&& caerEventPacketHeaderGetEventType(caerEventPacketContainerIteratorElement) == POLARITY_EVENT) {
			packetSizeX = sshsNodeGetShort(sourceInfoNode, "dvsSizeX");
			packetSizeY = sshsNodeGetShort(sourceInfoNode, "dvsSizeY");
		}
		else if (sshsNodeAttributeExists(sourceInfoNode, "apsSizeX", SSHS_SHORT)
			&& caerEventPacketHeaderGetEventType(caerEventPacketContainerIteratorElement) == FRAME_EVENT) {
			packetSizeX = sshsNodeGetShort(sourceInfoNode, "apsSizeX");
			packetSizeY = sshsNodeGetShort(sourceInfoNode, "apsSizeY");
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
	CAER_EVENT_PACKET_CONTAINER_ITERATOR_END

	// Search for renderer in list.
	caerVisualizerRenderer renderer = NULL;

	char *rendererChoice = sshsNodeGetString(moduleData->moduleNode, "renderer");

	for (size_t i = 0; i < (sizeof(caerVisualizerRendererList) / sizeof(struct caer_visualizer_renderers)); i++) {
		if (strcmp(rendererChoice, caerVisualizerRendererList[i].name) == 0) {
			renderer = caerVisualizerRendererList[i].renderer;
			break;
		}
	}

	free(rendererChoice);

	// Search for event handler in list.
	caerVisualizerEventHandler eventHandler = NULL;

	char *eventHandlerChoice = sshsNodeGetString(moduleData->moduleNode, "eventHandler");

	for (size_t i = 0; i < (sizeof(caerVisualizerHandlerList) / sizeof(struct caer_visualizer_handlers)); i++) {
		if (strcmp(eventHandlerChoice, caerVisualizerHandlerList[i].name) == 0) {
			eventHandler = caerVisualizerHandlerList[i].handler;
			break;
		}
	}

	free(eventHandlerChoice);

	moduleData->moduleState = caerVisualizerInit(renderer, eventHandler, sizeX, sizeY, VISUALIZER_DEFAULT_ZOOM, true,
		moduleData, sourceID);
	if (moduleData->moduleState == NULL) {
		return (false);
	}

	return (true);
}

static void caerVisualizerModuleExit(caerModuleData moduleData) {
	// Shut down rendering.
	caerVisualizerExit(moduleData->moduleState);
	moduleData->moduleState = NULL;
}

static void caerVisualizerModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	// Reset counters for statistics on reset.
	caerVisualizerReset(moduleData->moduleState);
}

static void caerVisualizerModuleRun(caerModuleData moduleData, caerEventPacketContainer in,
	caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	// Without a packet container with events, we cannot initialize or render anything.
	if (in == NULL || caerEventPacketContainerGetEventsNumber(in) == 0) {
		return;
	}

	// Initialize visualizer. Needs information from a packet (the source ID)!
	if (moduleData->moduleState == NULL) {
		if (!caerVisualizerModuleInitSize(moduleData, in)) {
			return;
		}
	}

	// Render given packet container.
	caerVisualizerUpdate(moduleData->moduleState, in);
}
