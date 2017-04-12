/*
 * mainloops.c
 *
 *  Created on: Dec 9, 2013
 *      Author: chtekk
 */

#include "mainloop.h"
#include <csignal>
#include <climits>
#include <unistd.h>
#include <string>
#include <algorithm>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/filesystem.hpp>
#include <dlfcn.h>
#include <iostream>
#include <libcaercpp/libcaer.hpp>
using namespace libcaer::log;

struct moduleInfo {
	int16_t id;
	std::string shortName;
	std::string type;
	std::string inputDefinition;
	sshsNode configNode;
	void *libraryHandle;
	caerModuleInfo libraryInfo;
};

static struct {
	sshsNode mainloopNode;
	atomic_bool running;
	atomic_uint_fast32_t dataAvailable;
	std::unordered_map<int16_t, moduleInfo> modules;
} glMainloopData;

static std::vector<boost::filesystem::path> modulePaths;

static int caerMainloopRunner(void);
static void caerMainloopSignalHandler(int signal);
static void caerMainloopRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
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

	// Search for available modules. Will be loaded as needed later.
	// Initialize with default search directory.
	sshsNode moduleSearchNode = sshsGetNode(sshsGetGlobal(), "/caer/modules/");

	boost::filesystem::path moduleSearchDir = boost::filesystem::current_path();
	moduleSearchDir.append("modules/");

	sshsNodeCreateString(moduleSearchNode, "moduleSearchPath", moduleSearchDir.generic_string().c_str(), 2, PATH_MAX,
		SSHS_FLAGS_NORMAL);

	// Now get actual search directory.
	char *moduleSearchPathC = sshsNodeGetString(moduleSearchNode, "moduleSearchPath");
	std::string moduleSearchPath = moduleSearchPathC;
	free(moduleSearchPathC);

	const std::regex moduleRegex("\\w+\\.(so|dll)");

	std::for_each(boost::filesystem::recursive_directory_iterator(moduleSearchPath),
		boost::filesystem::recursive_directory_iterator(),
		[&moduleRegex](const boost::filesystem::directory_entry &e) {
			if (boost::filesystem::is_regular_file(e.path()) && std::regex_match(e.path().filename().string(), moduleRegex)) {
				modulePaths.push_back(e.path());
			}
		});

	std::sort(modulePaths.begin(), modulePaths.end());

	// Configure and launch main-loop.
	glMainloopData.mainloopNode = sshsGetNode(sshsGetGlobal(), "/");

	// Mainloop disabled by default
	atomic_store(&glMainloopData.running, false);

	// Add per-mainloop shutdown hooks to SSHS for external control.
	sshsNodeCreateBool(glMainloopData.mainloopNode, "running", false, SSHS_FLAGS_NORMAL);
	sshsNodeAddAttributeListener(glMainloopData.mainloopNode, NULL, &caerMainloopRunningListener);

	caerMainloopRunner();
}

static int caerMainloopRunner(void) {
	// At this point configuration is already loaded, so let's see if everything
	// we need to build and run a mainloop is really there.
	// Each node in the root / is a module, with a short-name as node-name,
	// an ID (16-bit integer, "moduleId") as attribute, a module type (string,
	// "moduleType") as attribute, and an attribute that defines connectivity
	// to other modules (string, "moduleInput").
	size_t numModules = 0;
	sshsNode *modules = sshsNodeGetChildren(glMainloopData.mainloopNode, &numModules);
	if (modules == NULL || numModules == 0) {
		// Empty config, notify and wait.
		// TODO: notify and wait.
		log(logLevel::ERROR, "Mainloop", "No module configuration found.");
	}

	for (size_t i = 0; i < numModules; i++) {
		sshsNode module = modules[i];
		std::string moduleName = sshsNodeGetName(module);

		if (moduleName == "caer") {
			// Skip system configuration, not a module.
			continue;
		}

		if (!sshsNodeAttributeExists(module, "moduleId", SSHS_SHORT)
			|| !sshsNodeAttributeExists(module, "moduleType", SSHS_STRING)
			|| !sshsNodeAttributeExists(module, "moduleInput", SSHS_STRING)) {
			// Missing required attributes, notify and skip.
			// TODO: notify.
			log(logLevel::ERROR, "Mainloop", "Missing core attributes.");
			continue;
		}

		moduleInfo info = { };
		info.id = sshsNodeGetShort(module, "moduleId");
		info.shortName = moduleName;
		char *moduleType = sshsNodeGetString(module, "moduleType");
		info.type = moduleType;
		free(moduleType);
		char *moduleInput = sshsNodeGetString(module, "moduleInput");
		info.inputDefinition = moduleInput;
		free(moduleInput);
		info.configNode = module;

		// Put data into unordered set that holds all valid modules.
		// This also ensure the numerical ID is unique!
		auto result = glMainloopData.modules.insert(std::make_pair(info.id, info));
		if (!result.second) {
			// Failed insertion, key (ID) already exists!
			// TODO: notify.
			log(logLevel::ERROR, "Mainloop", "ID already exists.");
			continue;
		}
	}

	// At this point we have a map with all the valid modules and their info.
	// If that map is empty, there was nothing valid...
	if (glMainloopData.modules.empty()) {
		// TODO: notify.
		log(logLevel::ERROR, "Mainloop", "No modules.");
		return (EXIT_FAILURE);
	}
	else {
		log(logLevel::ERROR, "Mainloop", "%d modules found.", glMainloopData.modules.size());
	}

	// Let's load the modules and get their internal info.
	for (auto &m : glMainloopData.modules) {
		// For each module, we search if a path exists to load it from.
		// If yes, we do so. The various OS's shared library load mechanisms
		// will keep track of reference count if same module is loaded
		// multiple times.
		boost::filesystem::path modulePath;

		for (auto &p : modulePaths) {
			if (m.second.type == p.stem().string()) {
				// Found a module with same name!
				modulePath = p;
			}
		}

		if (modulePath.empty()) {
			log(logLevel::ERROR, "Mainloop", "No shared library found for module '%s'.", m.second.type.c_str());
			continue;
		}

		log(logLevel::NOTICE, "Mainloop", "Loading module library '%s'.", modulePath.c_str());

		void *moduleLibrary = dlopen(modulePath.c_str(), RTLD_NOW);
		if (moduleLibrary == NULL) {
			// Failed to load shared library!
			// TODO: notify.
			log(logLevel::ERROR, "Mainloop", "Failed to load library '%s', error: '%s'.", modulePath.c_str(),
				dlerror());
			exit(EXIT_FAILURE);
		}

		caerModuleInfo (*getInfo)(void) = (caerModuleInfo (*)(void)) dlsym(moduleLibrary, "caerModuleGetInfo");
		if (getInfo == NULL) {
			// Failed to find symbol in shared library!
			// TODO: notify.
			log(logLevel::ERROR, "Mainloop", "Failed to find symbol in library '%s', error: '%s'.", modulePath.c_str(),
				dlerror());
			dlclose(moduleLibrary);
			continue;
		}

		caerModuleInfo info = (*getInfo)();
		if (info == NULL) {
			log(logLevel::ERROR, "Mainloop", "Failed to get info from library '%s', error: '%s'.", modulePath.c_str(),
				dlerror());
			dlclose(moduleLibrary);
			continue;
		}

		// Check that the modules respect the basic I/O definition requirements.
		if (info->type == CAER_MODULE_INPUT) {
			if (info->inputStreams != NULL || info->inputStreamsSize != 0 || info->outputStreams == NULL
				|| info->outputStreamsSize == 0) {
				log(logLevel::ERROR, "Mainloop", "Module '%s' type INPUT has wrong I/O event stream definitions.",
					info->name);
				dlclose(moduleLibrary);
				continue;
			}
		}
		else if (info->type == CAER_MODULE_OUTPUT) {
			if (info->inputStreams == NULL || info->inputStreamsSize == 0 || info->outputStreams != NULL
				|| info->outputStreamsSize != 0) {
				log(logLevel::ERROR, "Mainloop", "Module '%s' type OUTPUT has wrong I/O event stream definitions.",
					info->name);
				dlclose(moduleLibrary);
				continue;
			}
		}
		else {
			// CAER_MODULE_PROCESSOR
			if (info->inputStreams == NULL || info->inputStreamsSize == 0 || info->outputStreams == NULL
				|| info->outputStreamsSize == 0) {
				log(logLevel::ERROR, "Mainloop", "Module '%s' type PROCESSOR has wrong I/O event stream definitions.",
					info->name);
				dlclose(moduleLibrary);
				continue;
			}
		}

		m.second.libraryHandle = moduleLibrary;
		m.second.libraryInfo = info;
	}

	std::vector<moduleInfo> execModules;
	std::vector<moduleInfo> inputModules;
	std::vector<moduleInfo> outputModules;
	std::vector<moduleInfo> processorModules;

	// Now we must parse, validate and create the connectivity map between modules.
	for (auto &m : glMainloopData.modules) {
		// If the module doesn't have any input definition (from where to take data),
		// then it must be a module that exclusively creates data, an INPUT module.
		if (m.second.inputDefinition.empty()) {
			if (m.second.libraryInfo->type == CAER_MODULE_INPUT) {
				// Good, is an input module. Add to list of inputs, and add to list
				// of modules to execute at the top.
				inputModules.push_back(m.second);
				execModules.insert(execModules.begin(), m.second);
			}
			else {
				// Error, invalid input definition on INPUT module.
				log(logLevel::ERROR, "Mainloop",
					"Invalid moduleInput config for module '%s', module is INPUT but config is not empty.",
					m.second.shortName);
				exit(EXIT_FAILURE);
			}
		}

		// inputDefinition is not empty, so we're a module that consumes data,
		// either an OUTPUT or a PROCESSOR.
		if (m.second.libraryInfo->type == CAER_MODULE_OUTPUT) {

		}
		else {
			// CAER_MODULE_PROCESSOR
		}
	}

	for (auto m : inputModules) {
		std::cout << m.id << "-INPUT-" << m.shortName << std::endl;
	}

	for (auto m : processorModules) {
		std::cout << m.id << "-PROCESSOR-" << m.shortName << std::endl;
	}

	for (auto m : outputModules) {
		std::cout << m.id << "-OUTPUT-" << m.shortName << std::endl;
	}

	for (auto m : execModules) {
		std::cout << m.id << "-[Type: " << m.libraryInfo->type << "]-" << m.shortName << std::endl;
	}

//	// Enable memory recycling.
//	utarray_new(glMainloopData.memoryToFree, &ut_genericFree_icd);
//
//	// Store references to all active modules, separated by type.
//	utarray_new(glMainloopData.inputModules, &ut_ptr_icd);
//	utarray_new(glMainloopData.outputModules, &ut_ptr_icd);
//	utarray_new(glMainloopData.processorModules, &ut_ptr_icd);
//
//	// TODO: init modules.
//
//	// If no data is available, sleep for a millisecond to avoid wasting resources.
//	struct timespec noDataSleep = { .tv_sec = 0, .tv_nsec = 1000000 };
//
//	// Wait for someone to toggle the module shutdown flag OR for the loop
//	// itself to signal termination.
//	size_t sleepCount = 0;
//
//	while (atomic_load_explicit(&glMainloopData.running, memory_order_relaxed)) {
//		// Run only if data available to consume, else sleep. But make a run
//		// anyway each second, to detect new devices for example.
//		if (atomic_load_explicit(&glMainloopData.dataAvailable, memory_order_acquire) > 0 || sleepCount > 1000) {
//			sleepCount = 0;
//
//			// TODO: execute modules.
//
//			// After each successful main-loop run, free the memory that was
//			// accumulated for things like packets, valid only during the run.
//			struct genericFree *memFree = NULL;
//			while ((memFree = (struct genericFree *) utarray_next(glMainloopData.memoryToFree, memFree)) != NULL) {
//				memFree->func(memFree->memPtr);
//			}
//			utarray_clear(glMainloopData.memoryToFree);
//		}
//		else {
//			sleepCount++;
//			thrd_sleep(&noDataSleep, NULL);
//		}
//	}
//
//	// Shutdown all modules.
//	for (caerModuleData m = glMainloopData.modules; m != NULL; m = m->hh.next) {
//		sshsNodePutBool(m->moduleNode, "running", false);
//	}
//
//	// Run through the loop one last time to correctly shutdown all the modules.
//	// TODO: exit modules.
//
//	// Do one last memory recycle run.
//	struct genericFree *memFree = NULL;
//	while ((memFree = (struct genericFree *) utarray_next(glMainloopData.memoryToFree, memFree)) != NULL) {
//		memFree->func(memFree->memPtr);
//	}
//
//	// Clear and free all allocated arrays.
//	utarray_free(glMainloopData.memoryToFree);
//
//	utarray_free(glMainloopData.inputModules);
//	utarray_free(glMainloopData.outputModules);
//	utarray_free(glMainloopData.processorModules);

	return (EXIT_SUCCESS);
}

// Only use this inside the mainloop-thread, not inside any other thread,
// like additional data acquisition threads or output threads.
void caerMainloopFreeAfterLoop(void (*func)(void *mem), void *memPtr) {

}

void caerMainloopDataNotifyIncrease(void *p) {
	glMainloopData.dataAvailable.fetch_add(1, std::memory_order_release);
}

void caerMainloopDataNotifyDecrease(void *p) {
	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	glMainloopData.dataAvailable.fetch_sub(1, std::memory_order_relaxed);
}

static inline caerModuleData findSourceModule(uint16_t sourceID) {

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

}

void caerMainloopResetOutputs(uint16_t sourceID) {

}

void caerMainloopResetProcessors(uint16_t sourceID) {

}

static void caerMainloopSignalHandler(int signal) {
	UNUSED_ARGUMENT(signal);

	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	atomic_store(&glMainloopData.running, false);
}

static void caerMainloopRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(userData);

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		// Shutdown requested! This goes to the mainloop/system 'running' atomic flags.
		atomic_store(&glMainloopData.running, changeValue.boolean);
	}
}
