/*
 * log.h
 *
 *  Created on: Dec 30, 2013
 *      Author: llongi
 */

#ifndef LOG_H_
#define LOG_H_

#include "main.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int CAER_LOG_FILE_FD;

void caerLogInit(void);
void caerLogModule(caerModuleData moduleData, enum caer_log_level logLevel, const char *format, ...) ATTRIBUTE_FORMAT(3);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H_ */
