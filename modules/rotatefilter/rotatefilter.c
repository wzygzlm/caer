#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"

#include <math.h>
#include <libcaer/events/polarity.h>

struct rotate_state {
	bool swapXY;
	bool rotate90deg;
	bool invertX;
	bool invertY;
	float angleDeg;
};

typedef struct rotate_state *RotateState;

static bool caerRotateInit(caerModuleData moduleData);
static void caerRotateRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerRotateConfig(caerModuleData moduleData);
static void caerRotateExit(caerModuleData moduleData);
static void checkBoundary(int *x, int *y, int sizeX, int sizeY);

static const struct caer_module_functions caerRotateFunctions = { .moduleInit = &caerRotateInit, .moduleRun =
	&caerRotateRun, .moduleConfig = &caerRotateConfig, .moduleExit = &caerRotateExit };

static const struct caer_event_stream_in caerRotateInputs[] = {
	{ .type = POLARITY_EVENT, .number = 1, .readOnly = false } };

static const struct caer_module_info moduleInfo = { .version = 1, .name = "Rotate", .type = CAER_MODULE_PROCESSOR,
	.memSize = sizeof(struct rotate_state), .functions = &caerRotateFunctions, .inputStreams = caerRotateInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(caerRotateInputs), .outputStreams = NULL, .outputStreamsSize = 0 };

caerModuleInfo caerModuleGetInfo(void) {
	return (&moduleInfo);
}

static bool caerRotateInit(caerModuleData moduleData) {
	sshsNodeCreateBool(moduleData->moduleNode, "swapXY", false, SSHS_FLAGS_NORMAL, "Swap X and Y axes.");
	sshsNodeCreateBool(moduleData->moduleNode, "rotate90deg", false, SSHS_FLAGS_NORMAL, "Rotate by 90 degrees.");
	sshsNodeCreateBool(moduleData->moduleNode, "invertX", false, SSHS_FLAGS_NORMAL, "Invert X axis.");
	sshsNodeCreateBool(moduleData->moduleNode, "invertY", false, SSHS_FLAGS_NORMAL, "Invert Y axis.");
	sshsNodeCreateFloat(moduleData->moduleNode, "angleDeg", 0.0f, 0.0f, 360.0f, SSHS_FLAGS_NORMAL,
		"Rotate by arbitrary angle.");

	RotateState state = moduleData->moduleState;

	state->swapXY = sshsNodeGetBool(moduleData->moduleNode, "swapXY");
	state->rotate90deg = sshsNodeGetBool(moduleData->moduleNode, "rotate90deg");
	state->invertX = sshsNodeGetBool(moduleData->moduleNode, "invertX");
	state->invertY = sshsNodeGetBool(moduleData->moduleNode, "invertY");
	state->angleDeg = sshsNodeGetFloat(moduleData->moduleNode, "angleDeg");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}

static void caerRotateRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	caerPolarityEventPacket polarity = (caerPolarityEventPacket) caerEventPacketContainerFindEventPacketByType(in,
		POLARITY_EVENT);

	//Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	RotateState state = moduleData->moduleState;

	int16_t sourceID = caerEventPacketHeaderGetEventSource(&polarity->packetHeader);
	sshsNode sourceInfoNodeCA = caerMainloopGetSourceInfo(sourceID);
	int16_t sizeX = sshsNodeGetShort(sourceInfoNodeCA, "polaritySizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNodeCA, "polaritySizeY");

	// Iterate over events
	CAER_POLARITY_ITERATOR_VALID_START(polarity)

	// Get values on which to operate.
		uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
		uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

		// do rotate
		if (state->swapXY) {
			int newX = y;
			int newY = x;
			checkBoundary(&newX, &newY, sizeX, sizeY);
			caerPolarityEventSetX(caerPolarityIteratorElement, newX);
			caerPolarityEventSetY(caerPolarityIteratorElement, newY);

		}
		if (state->rotate90deg) {
			int newX = (sizeY - y - 1);
			int newY = x;
			checkBoundary(&newX, &newY, sizeX, sizeY);
			caerPolarityEventSetX(caerPolarityIteratorElement, newX);
			caerPolarityEventSetY(caerPolarityIteratorElement, newY);
		}
		if (state->invertX) {
			caerPolarityEventSetX(caerPolarityIteratorElement, (sizeX - x - 1));
		}
		if (state->invertY) {
			caerPolarityEventSetY(caerPolarityIteratorElement, (sizeY - y - 1));
		}
		if (state->angleDeg != 0.0f) {
			float cosAng = cos(state->angleDeg * M_PI / 180.0f);
			float sinAng = sin(state->angleDeg * M_PI / 180.0f);
			int x2 = x - sizeX / 2;
			int y2 = y - sizeY / 2;
			int x3 = (int) (round(+cosAng * x2 - sinAng * y2));
			int y3 = (int) (round(+sinAng * x2 + cosAng * y2));
			int newX = x3 + sizeX / 2;
			int newY = y3 + sizeY / 2;
			checkBoundary(&newX, &newY, sizeX, sizeY);
			caerPolarityEventSetX(caerPolarityIteratorElement, newX);
			caerPolarityEventSetY(caerPolarityIteratorElement, newY);
		}

	CAER_POLARITY_ITERATOR_VALID_END
}

static void checkBoundary(int* x, int* y, int sizeX, int sizeY) {
	if (*x >= sizeX) {
		*x = sizeX - 1;
	}
	if (*x < 0) {
		*x = 0;
	}
	if (*y >= sizeY) {
		*y = sizeY - 1;
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
