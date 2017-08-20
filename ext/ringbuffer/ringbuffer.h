/*
 * ringbuffer.h
 *
 *  Created on: Dec 10, 2013
 *      Author: llongi
 */

#ifndef RINGBUFFER_H_
#define RINGBUFFER_H_

#ifdef __cplusplus

#include <cstdlib>
#include <cstdint>

#else

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Support symbol export on Windows GCC/Clang.
#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32)) && (defined(__GNUC__) || defined(__clang__))
#define CAER_SYMBOL_EXPORT __attribute__ ((__dllexport__))
#else
#define CAER_SYMBOL_EXPORT
#endif

typedef struct ring_buffer *RingBuffer;

RingBuffer ringBufferInit(size_t size) CAER_SYMBOL_EXPORT;
void ringBufferFree(RingBuffer rBuf) CAER_SYMBOL_EXPORT;
bool ringBufferPut(RingBuffer rBuf, void *elem) CAER_SYMBOL_EXPORT;
bool ringBufferFull(RingBuffer rBuf) CAER_SYMBOL_EXPORT;
void *ringBufferGet(RingBuffer rBuf) CAER_SYMBOL_EXPORT;
void *ringBufferLook(RingBuffer rBuf) CAER_SYMBOL_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* RINGBUFFER_H_ */
