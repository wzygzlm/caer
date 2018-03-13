/*
 * Public header for support library.
 * Modules can use this and link to it.
 */

#ifndef LIB_MAINLOOP_H_
#define LIB_MAINLOOP_H_

#include "module.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

void caerMainloopDataNotifyIncrease(void *p);
void caerMainloopDataNotifyDecrease(void *p);
bool caerMainloopModuleExists(int16_t id);
bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type);
bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId);

int16_t *caerMainloopGetModuleInputIDs(int16_t id, size_t *inputsSize);

sshsNode caerMainloopGetSourceNode(int16_t sourceID);
sshsNode caerMainloopGetSourceInfo(int16_t sourceID);
void *caerMainloopGetSourceState(int16_t sourceID);
sshsNode caerMainloopGetModuleNode(int16_t sourceID);

void caerMainloopResetInputs(int16_t sourceID);
void caerMainloopResetOutputs(int16_t sourceID);
void caerMainloopResetProcessors(int16_t sourceID);

#ifdef __cplusplus
}
#endif

#endif /* LIB_MAINLOOP_H_ */
