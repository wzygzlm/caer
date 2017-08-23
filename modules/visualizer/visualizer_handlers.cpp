#include "visualizer_handlers.hpp"

#include <cmath>
#include <boost/algorithm/string.hpp>

#include <libcaercpp/devices/dynapse.hpp> // Only for constants.

// Default event handlers.
static void caerVisualizerEventHandlerNeuronMonitor(caerVisualizerPublicState state, const sf::Event &event);
static void caerVisualizerEventHandlerInput(caerVisualizerPublicState state, const sf::Event &event);

const std::string caerVisualizerEventHandlerListOptionsString = "None,Neuron_Monitor,Input";

const struct caer_visualizer_event_handler_info caerVisualizerEventHandlerList[] = { { "None", nullptr }, {
	"Neuron_Monitor", &caerVisualizerEventHandlerNeuronMonitor }, { "Input", &caerVisualizerEventHandlerInput } };

const size_t caerVisualizerEventHandlerListLength = (sizeof(caerVisualizerEventHandlerList)
	/ sizeof(struct caer_visualizer_event_handler_info));

static void caerVisualizerEventHandlerNeuronMonitor(caerVisualizerPublicState state, const sf::Event &event) {
	// This only works with actual hardware.
	const std::string moduleLibrary = sshsNodeGetStdString(state->eventSourceConfigNode, "moduleLibrary");
	if (moduleLibrary != "caer_dynapse") {
		return;
	}

	// On release of left click.
	if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Button::Left) {
		float positionX = (float) event.mouseButton.x;
		float positionY = (float) event.mouseButton.y;

		// Adjust coordinates according to zoom factor.
		float currentZoomFactor = sshsNodeGetFloat(state->visualizerConfigNode, "zoomFactor");
		if (currentZoomFactor > 1.0f) {
			positionX = floorf(positionX / currentZoomFactor);
			positionY = floorf(positionY / currentZoomFactor);
		}
		else if (currentZoomFactor < 1.0f) {
			positionX = floorf(positionX * currentZoomFactor);
			positionY = floorf(positionY * currentZoomFactor);
		}

		// Select chip. DYNAPSE_CONFIG_DYNAPSE_U0 default, doesn't need check.
		uint8_t chipId = DYNAPSE_CONFIG_DYNAPSE_U0;

		if (positionX >= DYNAPSE_CONFIG_XCHIPSIZE && positionY >= DYNAPSE_CONFIG_YCHIPSIZE) {
			chipId = DYNAPSE_CONFIG_DYNAPSE_U3;
		}
		else if (positionX < DYNAPSE_CONFIG_XCHIPSIZE && positionY >= DYNAPSE_CONFIG_YCHIPSIZE) {
			chipId = DYNAPSE_CONFIG_DYNAPSE_U2;
		}
		else if (positionX >= DYNAPSE_CONFIG_XCHIPSIZE && positionY < DYNAPSE_CONFIG_YCHIPSIZE) {
			chipId = DYNAPSE_CONFIG_DYNAPSE_U1;
		}

		// Adjust coordinates for chip.
		if (chipId == DYNAPSE_CONFIG_DYNAPSE_U3) {
			positionX -= DYNAPSE_CONFIG_XCHIPSIZE;
			positionY -= DYNAPSE_CONFIG_YCHIPSIZE;
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U2) {
			positionY -= DYNAPSE_CONFIG_YCHIPSIZE;
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U1) {
			positionX -= DYNAPSE_CONFIG_XCHIPSIZE;
		}

		// Select core. Core ID 0 default, doesn't need check.
		uint8_t coreId = 0;

		if (positionX < DYNAPSE_CONFIG_NEUCOL && positionY >= DYNAPSE_CONFIG_NEUROW) {
			coreId = 1;
		}
		else if (positionX >= DYNAPSE_CONFIG_NEUCOL && positionY < DYNAPSE_CONFIG_NEUROW) {
			coreId = 2;
		}
		else if (positionX >= DYNAPSE_CONFIG_NEUCOL && positionY >= DYNAPSE_CONFIG_NEUROW) {
			coreId = 3;
		}

		// Adjust coordinates for core.
		if (coreId == 1) {
			positionY -= DYNAPSE_CONFIG_NEUROW;
		}
		else if (coreId == 2) {
			positionX -= DYNAPSE_CONFIG_NEUCOL;
		}
		else if (coreId == 3) {
			positionX -= DYNAPSE_CONFIG_NEUCOL;
			positionY -= DYNAPSE_CONFIG_NEUROW;
		}

		// linear index
		uint32_t linearIndex = (U32T(positionY) * DYNAPSE_CONFIG_NEUCOL) + U32T(positionX);

		// TODO: switch to using SSHS.
		//caerDeviceConfigSet(NULL, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, U32T(chipId));
		//caerDeviceConfigSet(NULL, DYNAPSE_CONFIG_MONITOR_NEU, coreId, linearIndex);

		caerLog(CAER_LOG_DEBUG, "Visualizer", "Monitoring neuron - chip ID: %d, core ID: %d, neuron ID: %d.", chipId,
			coreId, linearIndex);
	}
}

static void caerVisualizerEventHandlerInput(caerVisualizerPublicState state, const sf::Event &event) {
	// This only works with an input module.
	const std::string moduleLibrary = sshsNodeGetStdString(state->eventSourceConfigNode, "moduleLibrary");
	if (!boost::algorithm::starts_with(moduleLibrary, "caer_input_")) {
		return;
	}

	// PAUSE.
	if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::Space) {
		bool pause = sshsNodeGetBool(state->eventSourceConfigNode, "pause");

		sshsNodePutBool(state->eventSourceConfigNode, "pause", !pause);
	}
	// SLOW DOWN.
	else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::S) {
		int timeSlice = sshsNodeGetInt(state->eventSourceConfigNode, "PacketContainerInterval");

		sshsNodePutInt(state->eventSourceConfigNode, "PacketContainerInterval", timeSlice / 2);
	}
	// SPEED UP.
	else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::F) {
		int timeSlice = sshsNodeGetInt(state->eventSourceConfigNode, "PacketContainerInterval");

		sshsNodePutInt(state->eventSourceConfigNode, "PacketContainerInterval", timeSlice * 2);
	}
}
