#include "visualizer_renderers.hpp"

#include "ext/sfml/line.hpp"
#include "ext/sfml/helpers.hpp"

#include <libcaercpp/events/polarity.hpp>
#include <libcaercpp/events/frame.hpp>
#include <libcaercpp/events/imu6.hpp>
#include <libcaercpp/events/point2d.hpp>
#include <libcaercpp/events/point4d.hpp>
#include <libcaercpp/events/spike.hpp>
#include <libcaercpp/devices/davis.hpp> // Only for constants.
#include <libcaercpp/devices/dynapse.hpp> // Only for constants.

static bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityEvents("Polarity",
	&caerVisualizerRendererPolarityEvents);

static void *caerVisualizerRendererFrameEventsStateInit(caerVisualizerPublicState state);
static void caerVisualizerRendererFrameEventsStateExit(caerVisualizerPublicState state);
static bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererFrameEvents("Frame", &caerVisualizerRendererFrameEvents,
	false, &caerVisualizerRendererFrameEventsStateInit, &caerVisualizerRendererFrameEventsStateExit);

static bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererIMU6Events("IMU_6-axes", &caerVisualizerRendererIMU6Events);

static bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPoint2DEvents("2D_Points",
	&caerVisualizerRendererPoint2DEvents);

static bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererSpikeEvents("Spikes", &caerVisualizerRendererSpikeEvents);

static void *caerVisualizerRendererSpikeEventsRasterStateInit(caerVisualizerPublicState state);
static bool caerVisualizerRendererSpikeEventsRaster(caerVisualizerPublicState state,
	caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererSpikeEventsRaster("Spikes_Raster_Plot",
	&caerVisualizerRendererSpikeEventsRaster, false, &caerVisualizerRendererSpikeEventsRasterStateInit, nullptr);

static bool caerVisualizerRendererPolarityAndFrameEvents(caerVisualizerPublicState state,
	caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityAndFrameEvents("Polarity_and_Frames",
	&caerVisualizerRendererPolarityAndFrameEvents);

const std::string caerVisualizerRendererListOptionsString =
	"None,Polarity,Frame,IMU_6-axes,2D_Points,Spikes,Spikes_Raster_Plot,Polarity_and_Frames";

const struct caer_visualizer_renderer_info caerVisualizerRendererList[] = { { "None", nullptr }, rendererPolarityEvents,
	rendererFrameEvents, rendererIMU6Events, rendererPoint2DEvents, rendererSpikeEvents, rendererSpikeEventsRaster,
	rendererPolarityAndFrameEvents };

const size_t caerVisualizerRendererListLength = (sizeof(caerVisualizerRendererList)
	/ sizeof(struct caer_visualizer_renderer_info));

static bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader polarityPacketHeader = caerEventPacketContainerFindEventPacketByType(container,
		POLARITY_EVENT);

	// No packet of requested type or empty packet (no valid events).
	if (polarityPacketHeader == NULL || caerEventPacketHeaderGetEventValid(polarityPacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::PolarityEventPacket polarityPacket(polarityPacketHeader, false);

	std::vector<sf::Vertex> vertices((size_t) polarityPacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &polarityEvent : polarityPacket) {
		if (!polarityEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// ON polarity (green), OFF polarity (red).
		sfml::Helpers::addPixelVertices(vertices, polarityEvent.getX(), polarityEvent.getY(),
			state->renderZoomFactor.load(std::memory_order_relaxed),
			(polarityEvent.getPolarity()) ? (sf::Color::Green) : (sf::Color::Red));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

struct renderer_frame_events_state {
	sf::Sprite sprite[DAVIS_APS_ROI_REGIONS_MAX];
	sf::Texture texture[DAVIS_APS_ROI_REGIONS_MAX];
	std::vector<uint8_t> pixels[DAVIS_APS_ROI_REGIONS_MAX];
};

typedef struct renderer_frame_events_state *rendererFrameEventsState;

static void *caerVisualizerRendererFrameEventsStateInit(caerVisualizerPublicState state) {
	// Allocate memory via C++ for renderer state, since we use C++ objects directly.
	rendererFrameEventsState renderState = new renderer_frame_events_state();

	for (size_t i = 0; i < DAVIS_APS_ROI_REGIONS_MAX; i++) {
		// Create texture representing frame, set smoothing.
		renderState->texture[i].create(state->renderSizeX, state->renderSizeY);
		renderState->texture[i].setSmooth(false);

		// Assign texture to sprite.
		renderState->sprite[i].setTexture(renderState->texture[i]);

		// 32-bit RGBA pixels (8-bit per channel), standard CG layout.
		renderState->pixels[i].reserve(state->renderSizeX * state->renderSizeY * 4);
	}

	return (renderState);
}

static void caerVisualizerRendererFrameEventsStateExit(caerVisualizerPublicState state) {
	rendererFrameEventsState renderState = (rendererFrameEventsState) state->renderState;

	delete renderState;
}

static bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	rendererFrameEventsState renderState = (rendererFrameEventsState) state->renderState;

	caerEventPacketHeader framePacketHeader = caerEventPacketContainerFindEventPacketByType(container, FRAME_EVENT);

	// No packet of requested type or empty packet (no valid events).
	if (framePacketHeader == NULL || caerEventPacketHeaderGetEventValid(framePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::FrameEventPacket framePacket(framePacketHeader, false);

	// Get last valid frame for each possible ROI region.
	const libcaer::events::FrameEvent *frames[DAVIS_APS_ROI_REGIONS_MAX] = { nullptr };

	for (const auto &frame : framePacket) {
		if (frame.isValid()) {
			frames[frame.getROIIdentifier()] = &frame;
		}
	}

	// Only operate on the last, valid frame for each ROI region. At least one must exist (see check above).
	for (size_t i = 0; i < DAVIS_APS_ROI_REGIONS_MAX; i++) {
		// Skip non existent ROI regions.
		if (frames[i] == nullptr) {
			continue;
		}

		// 32-bit RGBA pixels (8-bit per channel), standard CG layout.
		switch (frames[i]->getChannelNumber()) {
			case libcaer::events::FrameEvent::colorChannels::GRAYSCALE: {
				for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frames[i]->getPixelsMaxIndex();) {
					uint8_t greyValue = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8);
					renderState->pixels[i][dstIdx++] = greyValue; // R
					renderState->pixels[i][dstIdx++] = greyValue; // G
					renderState->pixels[i][dstIdx++] = greyValue; // B
					renderState->pixels[i][dstIdx++] = UINT8_MAX; // A
				}
				break;
			}

			case libcaer::events::FrameEvent::colorChannels::RGB: {
				for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frames[i]->getPixelsMaxIndex();) {
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // R
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // G
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // B
					renderState->pixels[i][dstIdx++] = UINT8_MAX; // A
				}
				break;
			}

			case libcaer::events::FrameEvent::colorChannels::RGBA: {
				for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frames[i]->getPixelsMaxIndex();) {
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // R
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // G
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // B
					renderState->pixels[i][dstIdx++] = U8T(frames[i]->getPixelArrayUnsafe()[srcIdx++] >> 8); // A
				}
				break;
			}
		}

		renderState->texture[i].update(renderState->pixels[i].data(), U32T(frames[i]->getLengthX()),
			U32T(frames[i]->getLengthY()), U32T(frames[i]->getPositionX()), U32T(frames[i]->getPositionY()));

		renderState->sprite[i].setTextureRect(
			sf::IntRect(frames[i]->getPositionX(), frames[i]->getPositionY(), frames[i]->getLengthX(),
				frames[i]->getLengthY()));

		float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

		renderState->sprite[i].setPosition((float) frames[i]->getPositionX() * zoomFactor,
			(float) frames[i]->getPositionY() * zoomFactor);

		renderState->sprite[i].setScale(zoomFactor, zoomFactor);

		state->renderWindow->draw(renderState->sprite[i]);
	}

	return (true);
}

#define RESET_LIMIT_POS(VAL, LIMIT) if ((VAL) > (LIMIT)) { (VAL) = (LIMIT); }
#define RESET_LIMIT_NEG(VAL, LIMIT) if ((VAL) < (LIMIT)) { (VAL) = (LIMIT); }

static bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container) {
	caerEventPacketHeader imu6PacketHeader = caerEventPacketContainerFindEventPacketByType(container, IMU6_EVENT);

	if (imu6PacketHeader == NULL || caerEventPacketHeaderGetEventValid(imu6PacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::IMU6EventPacket imu6Packet(imu6PacketHeader, false);

	float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

	float scaleFactorAccel = 30 * zoomFactor;
	float scaleFactorGyro = 15 * zoomFactor;
	float lineThickness = 4 * zoomFactor;
	float maxSizeX = (float) state->renderSizeX * zoomFactor;
	float maxSizeY = (float) state->renderSizeY * zoomFactor;

	sf::Color accelColor = sf::Color::Green;
	sf::Color gyroColor = sf::Color::Magenta;

	float centerPointX = maxSizeX / 2;
	float centerPointY = maxSizeY / 2;

	float accelX = 0, accelY = 0, accelZ = 0;
	float gyroX = 0, gyroY = 0, gyroZ = 0;
	float temp = 0;

	// Iterate over valid IMU events and average them.
	// This somewhat smoothes out the rendering.
	for (const auto &imu6Event : imu6Packet) {
		accelX += imu6Event.getAccelX();
		accelY += imu6Event.getAccelY();
		accelZ += imu6Event.getAccelZ();

		gyroX += imu6Event.getGyroX();
		gyroY += imu6Event.getGyroY();
		gyroZ += imu6Event.getGyroZ();

		temp += imu6Event.getTemp();
	}

	// Normalize values.
	int32_t validEvents = imu6Packet.getEventValid();

	accelX /= (float) validEvents;
	accelY /= (float) validEvents;
	accelZ /= (float) validEvents;

	gyroX /= (float) validEvents;
	gyroY /= (float) validEvents;
	gyroZ /= (float) validEvents;

	temp /= (float) validEvents;

	// Acceleration X, Y as lines. Z as a circle.
	float accelXScaled = centerPointX - accelX * scaleFactorAccel;
	RESET_LIMIT_POS(accelXScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(accelXScaled, 1 + lineThickness);
	float accelYScaled = centerPointY - accelY * scaleFactorAccel;
	RESET_LIMIT_POS(accelYScaled, maxSizeY - 2 - lineThickness);
	RESET_LIMIT_NEG(accelYScaled, 1 + lineThickness);
	float accelZScaled = fabsf(accelZ * scaleFactorAccel);
	RESET_LIMIT_POS(accelZScaled, centerPointY - 2 - lineThickness); // Circle max.
	RESET_LIMIT_NEG(accelZScaled, 1); // Circle min.

	sfml::Line accelLine(sf::Vector2f(centerPointX, centerPointY), sf::Vector2f(accelXScaled, accelYScaled),
		lineThickness, accelColor);
	state->renderWindow->draw(accelLine);

	sf::CircleShape accelCircle(accelZScaled);
	sfml::Helpers::setOriginToCenter(accelCircle);
	accelCircle.setFillColor(sf::Color::Transparent);
	accelCircle.setOutlineColor(accelColor);
	accelCircle.setOutlineThickness(-lineThickness);
	accelCircle.setPosition(sf::Vector2f(centerPointX, centerPointY));

	state->renderWindow->draw(accelCircle);

	// Gyroscope pitch(X), yaw(Y), roll(Z) as lines.
	float gyroXScaled = centerPointY + gyroX * scaleFactorGyro;
	RESET_LIMIT_POS(gyroXScaled, maxSizeY - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroXScaled, 1 + lineThickness);
	float gyroYScaled = centerPointX + gyroY * scaleFactorGyro;
	RESET_LIMIT_POS(gyroYScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroYScaled, 1 + lineThickness);
	float gyroZScaled = centerPointX - gyroZ * scaleFactorGyro;
	RESET_LIMIT_POS(gyroZScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroZScaled, 1 + lineThickness);

	sfml::Line gyroLine1(sf::Vector2f(centerPointX, centerPointY), sf::Vector2f(gyroYScaled, gyroXScaled),
		lineThickness, gyroColor);
	state->renderWindow->draw(gyroLine1);

	sfml::Line gyroLine2(sf::Vector2f(centerPointX, centerPointY - 20), sf::Vector2f(gyroZScaled, centerPointY - 20),
		lineThickness, gyroColor);
	state->renderWindow->draw(gyroLine2);

	// TODO: enhance IMU renderer with more text info.
	if (state->font != nullptr) {
		char valStr[128];

		// Acceleration X/Y.
		snprintf(valStr, 128, "%.2f,%.2f g", (double) accelX, (double) accelY);

		sf::Text accelText(valStr, *state->font, 30);
		sfml::Helpers::setTextColor(accelText, accelColor);
		accelText.setPosition(sf::Vector2f(accelXScaled, accelYScaled));

		state->renderWindow->draw(accelText);

		// Temperature.
		snprintf(valStr, 128, "Temp: %.2f C", (double) temp);

		sf::Text tempText(valStr, *state->font, 30);
		sfml::Helpers::setTextColor(tempText, sf::Color::White);
		tempText.setPosition(sf::Vector2f(0, 0));

		state->renderWindow->draw(tempText);
	}

	return (true);
}

static bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader point2DPacketHeader = caerEventPacketContainerFindEventPacketByType(container, POINT2D_EVENT);

	if (point2DPacketHeader == NULL || caerEventPacketHeaderGetEventValid(point2DPacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::Point2DEventPacket point2DPacket(point2DPacketHeader, false);

	std::vector<sf::Vertex> vertices((size_t) point2DPacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &point2DEvent : point2DPacket) {
		if (!point2DEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// Render points in color blue.
		sfml::Helpers::addPixelVertices(vertices, point2DEvent.getX(), point2DEvent.getY(),
			state->renderZoomFactor.load(std::memory_order_relaxed), sf::Color::Blue);
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

static inline sf::Color dynapseCoreIdToColor(uint8_t coreId) {
	if (coreId == 3) {
		return (sf::Color::Yellow);
	}
	else if (coreId == 2) {
		return (sf::Color::Red);
	}
	else if (coreId == 1) {
		return (sf::Color::Magenta);
	}

	// Core ID 0 has default.
	return (sf::Color::Green);
}

static bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader spikePacketHeader = caerEventPacketContainerFindEventPacketByType(container, SPIKE_EVENT);

	if (spikePacketHeader == NULL || caerEventPacketHeaderGetEventValid(spikePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::SpikeEventPacket spikePacket(spikePacketHeader, false);

	std::vector<sf::Vertex> vertices((size_t) spikePacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &spikeEvent : spikePacket) {
		if (!spikeEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// Render spikes with different colors based on core ID.
		uint8_t coreId = spikeEvent.getSourceCoreID();
		sfml::Helpers::addPixelVertices(vertices, libcaer::devices::dynapse::spikeEventGetX(spikeEvent),
			libcaer::devices::dynapse::spikeEventGetY(spikeEvent), state->renderZoomFactor.load(std::memory_order_relaxed),
			dynapseCoreIdToColor(coreId));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

// How many timestemps and neurons to show per chip.
#define SPIKE_RASTER_PLOT_TIMESTEPS 500
#define SPIKE_RASTER_PLOT_NEURONS 256

static void *caerVisualizerRendererSpikeEventsRasterStateInit(caerVisualizerPublicState state) {
	// Reset render size to allow for more neurons and timesteps to be displayed.
	// This results in less scaling on the X and Y axes.
	// Also add 2 pixels on X/Y to compensate for the middle separation bars.
	caerVisualizerResetRenderSize(state, (SPIKE_RASTER_PLOT_TIMESTEPS * 2) + 2, (SPIKE_RASTER_PLOT_NEURONS * 2) + 2);

	return (CAER_VISUALIZER_RENDER_INIT_NO_MEM); // No allocated memory.
}

static bool caerVisualizerRendererSpikeEventsRaster(caerVisualizerPublicState state,
	caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader spikePacketHeader = caerEventPacketContainerFindEventPacketByType(container, SPIKE_EVENT);

	if (spikePacketHeader == NULL || caerEventPacketHeaderGetEventValid(spikePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::SpikeEventPacket spikePacket(spikePacketHeader, false);

	// find max and min TS, event packets MUST be ordered by time, that's
	// an invariant property, so we can just select first and last event.
	// Also time is always positive, so we can use unsigned ints.
	uint32_t minTimestamp = U32T(spikePacket[0].getTimestamp());
	uint32_t maxTimestamp = U32T(spikePacket[-1].getTimestamp());

	// time span, +1 to divide space correctly in scaleX.
	uint32_t timeSpan = maxTimestamp - minTimestamp + 1;

	// Get render sizes, subtract 2px for middle borders.
	float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

	float sizeX = (float) (state->renderSizeX - 2) * zoomFactor;
	float sizeY = (float) (state->renderSizeY - 2) * zoomFactor;

	// Two plots in each of X and Y directions.
	float scaleX = (sizeX / 2.0f) / (float) timeSpan;
	float scaleY = (sizeY / 2.0f) / (float) DYNAPSE_CONFIG_NUMNEURONS;

	std::vector<sf::Vertex> vertices((size_t) spikePacket.getEventNumber() * 4);

	// Render all spikes.
	for (const auto &spikeEvent : spikePacket) {
		uint32_t ts = U32T(spikeEvent.getTimestamp());
		ts = ts - minTimestamp;

		// X is based on time.
		uint32_t plotX = U32T(floorf((float ) ts * scaleX));

		uint8_t coreId = spikeEvent.getSourceCoreID();

		uint32_t linearIndex = spikeEvent.getNeuronID();
		linearIndex += (coreId * DYNAPSE_CONFIG_NUMNEURONS_CORE);

		// Y is based on all neurons.
		uint32_t plotY = U32T(floorf((float ) linearIndex * scaleY));

		// Move plot X/Y based on chip ID, to get four quadrants with four chips.
		uint8_t chipId = spikeEvent.getChipID();

		if (chipId == DYNAPSE_CONFIG_DYNAPSE_U3) {
			plotX += (sizeX / 2) + 2; // +2 for middle border!
			plotY += (sizeY / 2) + 2; // +2 for middle border!
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U2) {
			plotY += (sizeY / 2) + 2; // +2 for middle border!
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U1) {
			plotX += (sizeX / 2) + 2; // +2 for middle border!
		}
		// DYNAPSE_CONFIG_DYNAPSE_U0 no changes.

		// Draw pixels of raster plot (some neurons might be merged due to aliasing).
		sfml::Helpers::addPixelVertices(vertices, plotX, plotY, zoomFactor, dynapseCoreIdToColor(coreId), false);
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	// Draw middle borders, only once!
	sfml::Line horizontalBorderLine(sf::Vector2f(0, (state->renderSizeY * zoomFactor) / 2),
		sf::Vector2f((state->renderSizeX * zoomFactor), (state->renderSizeY * zoomFactor) / 2),
		2 * zoomFactor, sf::Color::White);
	state->renderWindow->draw(horizontalBorderLine);

	sfml::Line verticalBorderLine(sf::Vector2f((state->renderSizeX * zoomFactor) / 2, 0),
		sf::Vector2f((state->renderSizeX * zoomFactor) / 2, (state->renderSizeY * zoomFactor)),
		2 * zoomFactor, sf::Color::White);
	state->renderWindow->draw(verticalBorderLine);

	return (true);
}

static bool caerVisualizerRendererPolarityAndFrameEvents(caerVisualizerPublicState state,
	caerEventPacketContainer container) {
	bool drewFrameEvents = caerVisualizerRendererFrameEvents(state, container);

	bool drewPolarityEvents = caerVisualizerRendererPolarityEvents(state, container);

	return (drewFrameEvents || drewPolarityEvents);
}
