/*
 * mainloops.c
 *
 *  Created on: Dec 9, 2013
 *      Author: chtekk
 */

#include "mainloop.h"
#include <csignal>
#include <unistd.h>

static caerMainloopData glMainloopData = NULL;

static int caerMainloopRunner(void);
static void caerMainloopSignalHandler(int signal);
static void caerMainloopShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

void caerMainloopRun(void) {
	// Install signal handler for global shutdown.
#if defined(OS_WINDOWS)
	if (signal(SIGTERM, &caerMainloopSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_EMERGENCY, "Mainloop", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (signal(SIGINT, &caerMainloopSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_EMERGENCY, "Mainloop", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (signal(SIGBREAK, &caerMainloopSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_EMERGENCY, "Mainloop", "Failed to set signal handler for SIGBREAK. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	// Disable closing of the console window where cAER is executing.
	// While we do catch the signal (SIGBREAK) that such an action generates, it seems
	// we can't reliably shut down within the hard time window that Windows enforces when
	// pressing the close button (X in top right corner usually). This seems to be just
	// 5 seconds, and we can't guarantee full shutdown (USB, file writing, etc.) in all
	// cases within that time period (multiple cameras, modules etc. make this worse).
	// So we just disable that and force the user to CTRL+C, which works fine.
	HWND consoleWindow = GetConsoleWindow();
	if (consoleWindow != NULL) {
		HMENU systemMenu = GetSystemMenu(consoleWindow, false);
		EnableMenuItem(systemMenu, SC_CLOSE, MF_GRAYED);
	}
#else
	struct sigaction shutdown;

	shutdown.sa_handler = &caerMainloopSignalHandler;
	shutdown.sa_flags = 0;
	sigemptyset(&shutdown.sa_mask);
	sigaddset(&shutdown.sa_mask, SIGTERM);
	sigaddset(&shutdown.sa_mask, SIGINT);

	if (sigaction(SIGTERM, &shutdown, NULL) == -1) {
		caerLog(CAER_LOG_EMERGENCY, "Mainloop", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdown, NULL) == -1) {
		caerLog(CAER_LOG_EMERGENCY, "Mainloop", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	// Ignore SIGPIPE.
	signal(SIGPIPE, SIG_IGN);
#endif

	// Allocate memory for the main-loop.
	glMainloopData = (caerMainloopData) calloc(1, sizeof(struct caer_mainloop_data));
	if (glMainloopData == NULL) {
		caerLog(CAER_LOG_EMERGENCY, "Mainloop", "Failed to allocate memory for the main-loop. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	// Configure and launch main-loop.
	glMainloopData->mainloopNode = sshsGetNode(sshsGetGlobal(), "/");

	// Enable this main-loop.
	atomic_store(&glMainloopData->running, true);

	// Add per-mainloop shutdown hooks to SSHS for external control.
	sshsNodeCreateBool(glMainloopData->mainloopNode, "running", true, SSHS_FLAGS_NORMAL); // Always reset to true.
	sshsNodeAddAttributeListener(glMainloopData->mainloopNode, NULL, &caerMainloopShutdownListener);

	caerMainloopRunner();

	// Done with everything, free the remaining memory.
	free(glMainloopData);
}

// Only use this inside the mainloop-thread, not inside any other thread,
// like additional data acquisition threads or output threads.
caerModuleData caerMainloopFindModule(uint16_t moduleID, const char *moduleShortName, enum caer_module_type type) {
	caerModuleData moduleData;

	// This is only ever called from within modules running in a main-loop.
	// So always inside the same thread, needing thus no synchronization.
	HASH_FIND(hh, glMainloopData->modules, &moduleID, sizeof(uint16_t), moduleData);

	if (moduleData == NULL) {
		// Create module and initialize it. May fail!
		moduleData = caerModuleInitialize(moduleID, moduleShortName, glMainloopData->mainloopNode);
		if (moduleData != NULL) {
			HASH_ADD(hh, glMainloopData->modules, moduleID, sizeof(uint16_t), moduleData);

			// Register with mainloop. Add to appropriate type.
			if (type == CAER_MODULE_INPUT) {
				utarray_push_back(glMainloopData->inputModules, &moduleData);
			}
			else if (type == CAER_MODULE_OUTPUT) {
				utarray_push_back(glMainloopData->outputModules, &moduleData);
			}
			else {
				utarray_push_back(glMainloopData->processorModules, &moduleData);
			}
		}
	}
	else {
		// Guard against initializing different modules with the same ID.
		// Check that the moduleShortName and the first part of the subSystemModuleString
		// actually match. This is true for all modules, including devices currently.
		char *moduleSubSystemString = strchr(moduleData->moduleSubSystemString, '-') + 1;
		if (!caerStrEqualsUpTo(moduleShortName, moduleSubSystemString, strlen(moduleShortName))) {
			caerLog(CAER_LOG_ALERT, moduleShortName, "Module sub-system string and module short-name do not match. "
				"You're probably using the same ID for multiple modules! "
				"ID = %" PRIu16 ", subSystemString = %s, shortName = %s.", moduleID, moduleSubSystemString,
				moduleShortName);
			return (NULL);
		}
	}

	return (moduleData);
}

struct genericFree {
	void (*func)(void *mem);
	void *memPtr;
};

static const UT_icd ut_genericFree_icd = { sizeof(struct genericFree), NULL, NULL, NULL };

static int caerMainloopRunner(void) {
	// Enable memory recycling.
	utarray_new(glMainloopData->memoryToFree, &ut_genericFree_icd);

	// Store references to all active modules, separated by type.
	utarray_new(glMainloopData->inputModules, &ut_ptr_icd);
	utarray_new(glMainloopData->outputModules, &ut_ptr_icd);
	utarray_new(glMainloopData->processorModules, &ut_ptr_icd);

	// TODO: init modules.

	// If no data is available, sleep for a millisecond to avoid wasting resources.
	struct timespec noDataSleep = { .tv_sec = 0, .tv_nsec = 1000000 };

	// Wait for someone to toggle the module shutdown flag OR for the loop
	// itself to signal termination.
	size_t sleepCount = 0;

	while (atomic_load_explicit(&glMainloopData->running, memory_order_relaxed)) {
		// Run only if data available to consume, else sleep. But make a run
		// anyway each second, to detect new devices for example.
		if (atomic_load_explicit(&glMainloopData->dataAvailable, memory_order_acquire) > 0 || sleepCount > 1000) {
			sleepCount = 0;

			// TODO: execute modules.

			// After each successful main-loop run, free the memory that was
			// accumulated for things like packets, valid only during the run.
			struct genericFree *memFree = NULL;
			while ((memFree = (struct genericFree *) utarray_next(glMainloopData->memoryToFree, memFree)) != NULL) {
				memFree->func(memFree->memPtr);
			}
			utarray_clear(glMainloopData->memoryToFree);
		}
		else {
			sleepCount++;
			thrd_sleep(&noDataSleep, NULL);
		}
	}

	// Shutdown all modules.
	for (caerModuleData m = glMainloopData->modules; m != NULL; m = m->hh.next) {
		sshsNodePutBool(m->moduleNode, "running", false);
	}

	// Run through the loop one last time to correctly shutdown all the modules.
	// TODO: exit modules.

	// Do one last memory recycle run.
	struct genericFree *memFree = NULL;
	while ((memFree = (struct genericFree *) utarray_next(glMainloopData->memoryToFree, memFree)) != NULL) {
		memFree->func(memFree->memPtr);
	}

	// Clear and free all allocated arrays.
	utarray_free(glMainloopData->memoryToFree);

	utarray_free(glMainloopData->inputModules);
	utarray_free(glMainloopData->outputModules);
	utarray_free(glMainloopData->processorModules);

	return (EXIT_SUCCESS);
}

// Only use this inside the mainloop-thread, not inside any other thread,
// like additional data acquisition threads or output threads.
void caerMainloopFreeAfterLoop(void (*func)(void *mem), void *memPtr) {
	struct genericFree memFree = { .func = func, .memPtr = memPtr };

	utarray_push_back(glMainloopData->memoryToFree, &memFree);
}

// Only use this inside the mainloop-thread, not inside any other thread,
// like additional data acquisition threads or output threads.
caerMainloopData caerMainloopGetReference(void) {
	return (glMainloopData);
}

static inline caerModuleData findSourceModule(uint16_t sourceID) {
	caerModuleData moduleData;

	// This is only ever called from within modules running in a main-loop.
	// So always inside the same thread, needing thus no synchronization.
	HASH_FIND(hh, glMainloopData->modules, &sourceID, sizeof(uint16_t), moduleData);

	if (moduleData == NULL) {
		// This is impossible if used correctly, you can't have a packet with
		// an event source X and that event source doesn't exist ...
		caerLog(CAER_LOG_ALERT, sshsNodeGetName(glMainloopData->mainloopNode),
			"Impossible to get module data for source ID %" PRIu16 ".", sourceID);
		return (NULL);
	}

	return (moduleData);
}

sshsNode caerMainloopGetSourceNode(uint16_t sourceID) {
	caerModuleData moduleData = findSourceModule(sourceID);
	if (moduleData == NULL) {
		return (NULL);
	}

	return (moduleData->moduleNode);
}

sshsNode caerMainloopGetSourceInfo(uint16_t sourceID) {
	sshsNode sourceNode = caerMainloopGetSourceNode(sourceID);
	if (sourceNode == NULL) {
		return (NULL);
	}

	// All sources have a sub-node in SSHS called 'sourceInfo/'.
	return (sshsGetRelativeNode(sourceNode, "sourceInfo/"));
}

void *caerMainloopGetSourceState(uint16_t sourceID) {
	caerModuleData moduleData = findSourceModule(sourceID);
	if (moduleData == NULL) {
		return (NULL);
	}

	return (moduleData->moduleState);
}

void caerMainloopResetInputs(uint16_t sourceID) {
	caerModuleData *moduleData = NULL;

	while ((moduleData = (caerModuleData *) utarray_next(glMainloopData->inputModules, moduleData)) != NULL) {
		atomic_store(&(*moduleData)->doReset, sourceID);
	}
}

void caerMainloopResetOutputs(uint16_t sourceID) {
	caerModuleData *moduleData = NULL;

	while ((moduleData = (caerModuleData *) utarray_next(glMainloopData->outputModules, moduleData)) != NULL) {
		atomic_store(&(*moduleData)->doReset, sourceID);
	}
}

void caerMainloopResetProcessors(uint16_t sourceID) {
	caerModuleData *moduleData = NULL;

	while ((moduleData = (caerModuleData *) utarray_next(glMainloopData->processorModules, moduleData)) != NULL) {
		atomic_store(&(*moduleData)->doReset, sourceID);
	}
}

static void caerMainloopSignalHandler(int signal) {
	UNUSED_ARGUMENT(signal);

	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	atomic_store(&glMainloopData->running, false);
}

static void caerMainloopShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(userData);

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		// Shutdown requested! This goes to the mainloop/system 'running' atomic flags.
		atomic_store(&glMainloopData->running, changeValue.boolean);
	}
}
