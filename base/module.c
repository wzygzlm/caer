/*
 * module.c
 *
 *  Created on: Dec 14, 2013
 *      Author: chtekk
 */

#include "module.h"

static void caerModuleShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void caerModuleLogLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

void caerModuleSM(caerModuleFunctions moduleFunctions, caerModuleData moduleData, size_t memSize,
	caerEventPacketContainer in, caerEventPacketContainer *out) {
	bool running = atomic_load_explicit(&moduleData->running, memory_order_relaxed);

	if (moduleData->moduleStatus == CAER_MODULE_RUNNING && running) {
		if (atomic_load_explicit(&moduleData->configUpdate, memory_order_relaxed) != 0) {
			if (moduleFunctions->moduleConfig != NULL) {
				// Call config function, which will have to reset configUpdate.
				moduleFunctions->moduleConfig(moduleData);
			}
		}

		if (moduleFunctions->moduleRun != NULL) {
			moduleFunctions->moduleRun(moduleData, in, out);
		}

		if (atomic_load_explicit(&moduleData->doReset, memory_order_relaxed) != 0) {
			if (moduleFunctions->moduleReset != NULL) {
				// Call reset function. 'doReset' variable reset is done here.
				int16_t resetCallSourceID = I16T(atomic_exchange(&moduleData->doReset, 0));
				moduleFunctions->moduleReset(moduleData, resetCallSourceID);
			}
		}
	}
	else if (moduleData->moduleStatus == CAER_MODULE_STOPPED && running) {
		if (memSize != 0) {
			moduleData->moduleState = calloc(1, memSize);
			if (moduleData->moduleState == NULL) {
				return;
			}
		}
		else {
			// memSize is zero, so moduleState must be NULL.
			moduleData->moduleState = NULL;
		}

		if (moduleFunctions->moduleInit != NULL) {
			if (!moduleFunctions->moduleInit(moduleData)) {
				free(moduleData->moduleState);
				moduleData->moduleState = NULL;

				return;
			}
		}

		moduleData->moduleStatus = CAER_MODULE_RUNNING;
	}
	else if (moduleData->moduleStatus == CAER_MODULE_RUNNING && !running) {
		moduleData->moduleStatus = CAER_MODULE_STOPPED;

		if (moduleFunctions->moduleExit != NULL) {
			moduleFunctions->moduleExit(moduleData);
		}

		free(moduleData->moduleState);
		moduleData->moduleState = NULL;
	}
}

caerModuleData caerModuleInitialize(int16_t moduleID, const char *moduleName, sshsNode moduleNode) {
	// Allocate memory for the module.
	caerModuleData moduleData = calloc(1, sizeof(struct caer_module_data));
	if (moduleData == NULL) {
		caerLog(CAER_LOG_ALERT, moduleName, "Failed to allocate memory for module. Error: %d.", errno);
		return (NULL);
	}

	// Set module ID for later identification (used as quick key often).
	moduleData->moduleID = moduleID;

	// Set configuration node (so it's user accessible).
	moduleData->moduleNode = moduleNode;

	// Put module into startup state. 'running' flag is updated later based on user startup wishes.
	moduleData->moduleStatus = CAER_MODULE_STOPPED;

	// Setup default full log string name.
	size_t nameLength = strlen(moduleName);
	moduleData->moduleSubSystemString = malloc(nameLength + 1);
	if (moduleData->moduleSubSystemString == NULL) {
		free(moduleData);

		caerLog(CAER_LOG_ALERT, moduleName, "Failed to allocate subsystem string for module.");
		return (NULL);
	}

	strncpy(moduleData->moduleSubSystemString, moduleName, nameLength);
	moduleData->moduleSubSystemString[nameLength] = '\0';

	// Per-module log level support. Initialize with global log level value.
	sshsNodeCreateByte(moduleData->moduleNode, "logLevel", caerLogLevelGet(), CAER_LOG_EMERGENCY, CAER_LOG_DEBUG,
		SSHS_FLAGS_NORMAL, "Module-specific log-level.");
	atomic_store_explicit(&moduleData->moduleLogLevel, U8T(sshsNodeGetByte(moduleData->moduleNode, "logLevel")),
		memory_order_relaxed);
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleLogLevelListener);

	// Initialize shutdown controls. By default modules always run.
	sshsNodeCreateBool(moduleData->moduleNode, "runAtStartup", true, SSHS_FLAGS_NORMAL,
		"Start this module when the mainloop starts."); // Allow for users to disable a module at start.
	bool runModule = sshsNodeGetBool(moduleData->moduleNode, "runAtStartup");

	atomic_store_explicit(&moduleData->running, runModule, memory_order_relaxed);
	sshsNodeCreateBool(moduleData->moduleNode, "running", runModule, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "Module start/stop.");
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleShutdownListener);

	atomic_thread_fence(memory_order_release);

	return (moduleData);
}

void caerModuleDestroy(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleShutdownListener);
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleLogLevelListener);

	// Deallocate module memory. Module state has already been destroyed.
	free(moduleData->moduleSubSystemString);
	free(moduleData);
}

bool caerModuleSetSubSystemString(caerModuleData moduleData, const char *subSystemString) {
	// Allocate new memory for new string.
	size_t subSystemStringLenght = strlen(subSystemString);

	char *newSubSystemString = malloc(subSystemStringLenght + 1);
	if (newSubSystemString == NULL) {
		// Failed to allocate memory. Log this and don't use the new string.
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
			"Failed to allocate new sub-system string for module.");
		return (false);
	}

	// Copy new string into allocated memory.
	strncpy(newSubSystemString, subSystemString, subSystemStringLenght);
	newSubSystemString[subSystemStringLenght] = '\0';

	// Switch new string with old string and free old memory.
	free(moduleData->moduleSubSystemString);
	moduleData->moduleSubSystemString = newSubSystemString;

	return (true);
}

void caerModuleConfigUpdateReset(caerModuleData moduleData) {
	atomic_store(&moduleData->configUpdate, 0);
}

void caerModuleConfigDefaultListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData data = userData;

	// Simply set the config update flag to 1 on any attribute change.
	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		atomic_store(&data->configUpdate, 1);
	}
}

void caerModuleLog(caerModuleData moduleData, enum caer_log_level logLevel, const char *format, ...) {
	va_list argumentList;
	va_start(argumentList, format);
	caerLogVAFull(caerLogFileDescriptorsGetFirst(), caerLogFileDescriptorsGetSecond(),
		atomic_load_explicit(&moduleData->moduleLogLevel, memory_order_relaxed), logLevel,
		moduleData->moduleSubSystemString, format, argumentList);
	va_end(argumentList);
}

static void caerModuleShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData data = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		atomic_store(&data->running, changeValue.boolean);
	}
}

static void caerModuleLogLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData data = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BYTE && caerStrEquals(changeKey, "logLevel")) {
		atomic_store(&data->moduleLogLevel, U8T(changeValue.ibyte));
	}
}
