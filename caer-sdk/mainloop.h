/*
 * Public header for support library.
 * Modules can use this and link to it.
 */

#ifndef CAER_SDK_MAINLOOP_H_
#define CAER_SDK_MAINLOOP_H_

#include "module.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

void caerMainloopDataNotifyIncrease(void *p);
void caerMainloopDataNotifyDecrease(void *p);

bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId);

bool caerMainloopModuleExists(int16_t id);
enum caer_module_type caerMainloopModuleGetType(int16_t id);
sshsNode caerMainloopModuleGetConfigNode(int16_t id);
size_t caerMainloopModuleGetInputDeps(int16_t id, int16_t **inputDepIds);
size_t caerMainloopModuleGetOutputRevDeps(int16_t id, int16_t **outputRevDepIds);
void caerMainloopModuleResetOutputRevDeps(int16_t sourceID);

sshsNode caerMainloopGetSourceNode(int16_t sourceID); // Can be NULL.
void *caerMainloopGetSourceState(int16_t sourceID); // Can be NULL.
sshsNode caerMainloopGetSourceInfo(int16_t sourceID); // Can be NULL.

void caerMainloopResetInputs(int16_t sourceID);
void caerMainloopResetOutputs(int16_t sourceID);
void caerMainloopResetProcessors(int16_t sourceID);

// Deprecated.
static inline bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type) {
	return (caerMainloopModuleGetType(id) == type);
}

static inline int16_t *caerMainloopGetModuleInputIDs(int16_t id, size_t *inputsSize) {
	int16_t *inputs;
	size_t numInputs = caerMainloopModuleGetInputDeps(id, &inputs);

	// If inputsSize is known, allow not passing it in.
	if (inputsSize != NULL) {
		*inputsSize = numInputs;
	}

	return (inputs);
}

static inline sshsNode caerMainloopGetModuleNode(int16_t id) {
	return (caerMainloopModuleGetConfigNode(id));
}

#ifdef __cplusplus
}
#endif

#endif /* CAER_SDK_MAINLOOP_H_ */
