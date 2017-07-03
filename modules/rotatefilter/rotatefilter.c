#include "base/mainloop.h"
#include "base/module.h"

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#include <libcaer/events/polarity.h>

struct rotate_state {
	bool swapXY;
	bool rotate90deg;
	bool invertX;
	bool invertY;
	float angleDeg;
	uint16_t sizeX;
	uint16_t sizeY;
};

typedef struct rotate_state *RotateState;

static bool caerRotateInit(caerModuleData moduleData);
static void caerRotateRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerRotateConfig(caerModuleData moduleData);
static void caerRotateExit(caerModuleData moduleData);
static void checkBoundary(uint16_t *x, uint16_t *y, RotateState state);

static const struct caer_module_functions caerRotateFunctions = { .moduleInit = &caerRotateInit, .moduleRun =
	&caerRotateRun, .moduleConfig = &caerRotateConfig, .moduleExit = &caerRotateExit };

static const struct caer_event_stream_in caerRotateInputs[] = {
	{ .type = POLARITY_EVENT, .number = 1, .readOnly = false } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "Rotate", .description = "Rotate events.",
	.type = CAER_MODULE_PROCESSOR, .memSize = sizeof(struct rotate_state), .functions = &caerRotateFunctions,
	.inputStreams = caerRotateInputs, .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(caerRotateInputs), .outputStreams =
		NULL, .outputStreamsSize = 0 };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

static bool caerRotateInit(caerModuleData moduleData) {
	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	int16_t *inputs = caerMainloopGetModuleInputIDs(moduleData->moduleID, NULL);
	if (inputs == NULL) {
		return (false);
	}

	int16_t sourceID = inputs[0];
	free(inputs);

	sshsNodeCreateBool(moduleData->moduleNode, "swapXY", false, SSHS_FLAGS_NORMAL, "Swap X and Y axes.");
	sshsNodeCreateBool(moduleData->moduleNode, "rotate90deg", false, SSHS_FLAGS_NORMAL, "Rotate by 90 degrees.");
	sshsNodeCreateBool(moduleData->moduleNode, "invertX", false, SSHS_FLAGS_NORMAL, "Invert X axis.");
	sshsNodeCreateBool(moduleData->moduleNode, "invertY", false, SSHS_FLAGS_NORMAL, "Invert Y axis.");
	sshsNodeCreateFloat(moduleData->moduleNode, "angleDeg", 0.0f, 0.0f, 360.0f, SSHS_FLAGS_NORMAL,
		"Rotate by arbitrary angle.");

	RotateState state = moduleData->moduleState;

	// Update all settings.
	sshsNode sourceInfo = caerMainloopGetSourceInfo(sourceID);
	if (sourceInfo == NULL) {
		return (false);
	}

	state->sizeX = U16T(sshsNodeGetShort(sourceInfo, "polaritySizeX"));
	state->sizeY = U16T(sshsNodeGetShort(sourceInfo, "polaritySizeY"));

	caerRotateConfig(moduleData);

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerRotateRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	caerPolarityEventPacket polarity = (caerPolarityEventPacket) caerEventPacketContainerFindEventPacketByType(in,
		POLARITY_EVENT);

	// Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	RotateState state = moduleData->moduleState;

	// Iterate over valid events.
	CAER_POLARITY_ITERATOR_VALID_START(polarity)
	// Get values on which to operate.
		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

		if (state->swapXY) {
			uint16_t newX = y;
			uint16_t newY = x;
			checkBoundary(&newX, &newY, state);

			caerPolarityEventSetX(caerPolarityIteratorElement, newX);
			caerPolarityEventSetY(caerPolarityIteratorElement, newY);
		}

		if (state->rotate90deg) {
			uint16_t newX = (state->sizeY - 1 - y);
			uint16_t newY = x;
			checkBoundary(&newX, &newY, state);

			caerPolarityEventSetX(caerPolarityIteratorElement, newX);
			caerPolarityEventSetY(caerPolarityIteratorElement, newY);
		}

		if (state->invertX) {
			caerPolarityEventSetX(caerPolarityIteratorElement, (state->sizeX - 1 - x));
		}

		if (state->invertY) {
			caerPolarityEventSetY(caerPolarityIteratorElement, (state->sizeY - 1 - y));
		}

		if (state->angleDeg != 0.0f) {
			float cosAng = cosf(state->angleDeg * M_PI / 180.0f);
			float sinAng = sinf(state->angleDeg * M_PI / 180.0f);

			int x2 = x - (state->sizeX / 2);
			int y2 = y - (state->sizeY / 2);
			int x3 = (int) (roundf(+cosAng * x2 - sinAng * y2));
			int y3 = (int) (roundf(+sinAng * x2 + cosAng * y2));

			uint16_t newX = U16T(x3 + (state->sizeX / 2));
			uint16_t newY = U16T(y3 + (state->sizeY / 2));
			checkBoundary(&newX, &newY, state);

			caerPolarityEventSetX(caerPolarityIteratorElement, newX);
			caerPolarityEventSetY(caerPolarityIteratorElement, newY);
		}CAER_POLARITY_ITERATOR_VALID_END
}

static void checkBoundary(uint16_t *x, uint16_t *y, RotateState state) {
	if (*x >= state->sizeX) {
		*x = state->sizeX - 1;
	}
	if (*x < 0) {
		*x = 0;
	}
	if (*y >= state->sizeY) {
		*y = state->sizeY - 1;
	}
	if (*y < 0) {
		*y = 0;
	}
}

static void caerRotateConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	RotateState state = moduleData->moduleState;

	state->swapXY = sshsNodeGetBool(moduleData->moduleNode, "swapXY");
	state->rotate90deg = sshsNodeGetBool(moduleData->moduleNode, "rotate90deg");
	state->invertX = sshsNodeGetBool(moduleData->moduleNode, "invertX");
	state->invertY = sshsNodeGetBool(moduleData->moduleNode, "invertY");
	state->angleDeg = sshsNodeGetFloat(moduleData->moduleNode, "angleDeg");
}

static void caerRotateExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);
}
