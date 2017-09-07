/*
 * mainloop.h
 *
 *  Created on: Dec 9, 2013
 *      Author: chtekk
 */

#ifndef MAINLOOP_H_
#define MAINLOOP_H_

#include "main.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

void caerMainloopRun(void);

void caerMainloopDataNotifyIncrease(void *p) CAER_SYMBOL_EXPORT;
void caerMainloopDataNotifyDecrease(void *p) CAER_SYMBOL_EXPORT;
bool caerMainloopModuleExists(int16_t id) CAER_SYMBOL_EXPORT;
bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type) CAER_SYMBOL_EXPORT;
bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId) CAER_SYMBOL_EXPORT;

int16_t *caerMainloopGetModuleInputIDs(int16_t id, size_t *inputsSize) CAER_SYMBOL_EXPORT;

sshsNode caerMainloopGetSourceNode(int16_t sourceID) CAER_SYMBOL_EXPORT;
sshsNode caerMainloopGetSourceInfo(int16_t sourceID) CAER_SYMBOL_EXPORT;
void *caerMainloopGetSourceState(int16_t sourceID) CAER_SYMBOL_EXPORT;
sshsNode caerMainloopGetModuleNode(int16_t sourceID) CAER_SYMBOL_EXPORT;

void caerMainloopResetInputs(int16_t sourceID) CAER_SYMBOL_EXPORT;
void caerMainloopResetOutputs(int16_t sourceID) CAER_SYMBOL_EXPORT;
void caerMainloopResetProcessors(int16_t sourceID) CAER_SYMBOL_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* MAINLOOP_H_ */
