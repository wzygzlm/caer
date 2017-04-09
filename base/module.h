/*
 * module.h
 *
 *  Created on: Dec 14, 2013
 *      Author: chtekk
 */

#ifndef MODULE_H_
#define MODULE_H_

#include "main.h"

#ifdef __cplusplus

#include <atomic>
using namespace std;

#else

#include <stdatomic.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Module-related definitions.
enum caer_module_status {
	CAER_MODULE_STOPPED = 0,
	CAER_MODULE_RUNNING = 1,
};

enum caer_module_type {
	CAER_MODULE_INPUT = 0,
	CAER_MODULE_OUTPUT = 1,
	CAER_MODULE_PROCESSOR = 2,
};

struct caer_event_stream {
	int16_t type; // Use -1 for any type.
	int16_t number; // Use -1 for any number of.
};

typedef struct caer_event_stream const *caerEventStream;

#define CAER_EVENT_STREAM_SIZE(x) (sizeof(x) / sizeof(struct caer_event_stream))

struct caer_module_data {
	uint16_t moduleID;
	sshsNode moduleNode;
	enum caer_module_status moduleStatus;
	atomic_bool running;
	atomic_uint_fast32_t configUpdate;
	void *moduleState;
	char *moduleSubSystemString;
	atomic_uint_fast8_t moduleLogLevel;
	atomic_uint_fast32_t doReset;
	void *parentMainloop;
};

typedef struct caer_module_data *caerModuleData;

struct caer_module_functions {
	bool (* const moduleInit)(caerModuleData moduleData); // Can be NULL.
	void (* const moduleRun)(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
	void (* const moduleConfig)(caerModuleData moduleData); // Can be NULL.
	void (* const moduleExit)(caerModuleData moduleData); // Can be NULL.
	void (* const moduleReset)(caerModuleData moduleData, uint16_t resetCallSourceID); // Can be NULL.
};

typedef struct caer_module_functions const * caerModuleFunctions;

struct caer_module_info {
	uint32_t version;
	const char *name;
	enum caer_module_type type;
	size_t memSize;
	caerModuleFunctions functions;
	size_t inputStreamsSize;
	caerEventStream inputStreams;
	size_t outputStreamsSize;
	caerEventStream outputStreams;
};

typedef struct caer_module_info const * caerModuleInfo;

// Functions to be implemented:
caerModuleInfo caerModuleGetInfo(void);

// Functions available to call:
bool caerModuleSetSubSystemString(caerModuleData moduleData, const char *subSystemString);
void caerModuleConfigUpdateReset(caerModuleData moduleData);
void caerModuleConfigDefaultListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_H_ */
