/*
 * log.h
 *
 *  Created on: Dec 30, 2013
 *      Author: llongi
 */

#ifndef LOG_H_
#define LOG_H_

#include "caer-sdk/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int CAER_LOG_FILE_FD;

void caerLogInit(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H_ */
