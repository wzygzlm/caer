/*
 * misc.h
 *
 *  Created on: Dec 9, 2013
 *      Author: chtekk
 */

#ifndef MISC_H_
#define MISC_H_

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(OS_WINDOWS)
	void caerDaemonize(void);
#endif

void caerBitArrayCopy(uint8_t *src, size_t srcPos, uint8_t *dest, size_t destPos, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* MISC_H_ */
