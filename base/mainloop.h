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
void caerMainloopDataNotifyIncrease(void *p);
void caerMainloopDataNotifyDecrease(void *p);
bool caerMainloopModuleExists(int16_t id);
bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type);
bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId);

sshsNode caerMainloopGetSourceNode(int16_t sourceID);
sshsNode caerMainloopGetSourceInfo(int16_t sourceID);
void *caerMainloopGetSourceState(int16_t sourceID);

void caerMainloopResetInputs(int16_t sourceID);
void caerMainloopResetOutputs(int16_t sourceID);
void caerMainloopResetProcessors(int16_t sourceID);

#ifdef __cplusplus
}
#endif

#endif /* MAINLOOP_H_ */
