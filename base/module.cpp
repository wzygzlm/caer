/*
 * module.c
 *
 *  Created on: Dec 14, 2013
 *      Author: chtekk
 */

#include "module.h"

#include <regex>
#include <thread>
#include <mutex>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

static struct {
	std::vector<boost::filesystem::path> modulePaths;
	std::recursive_mutex modulePathsMutex;
} glModuleData;

static void caerModuleShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void caerModuleLogLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

void caerModuleConfigInit(sshsNode moduleNode) {
	// Per-module log level support. Initialize with global log level value.
	sshsNodeCreateByte(moduleNode, "logLevel", caerLogLevelGet(), CAER_LOG_EMERGENCY, CAER_LOG_DEBUG, SSHS_FLAGS_NORMAL,
		"Module-specific log-level.");

	// Initialize shutdown controls. By default modules always run.
	sshsNodeCreateBool(moduleNode, "runAtStartup", true, SSHS_FLAGS_NORMAL,
		"Start this module when the mainloop starts."); // Allow for users to disable a module at start.

	// Call module's configInit function to create default static config.
	const std::string moduleName = sshsNodeGetStdString(moduleNode, "moduleLibrary");

	// Load library to get module functions.
	std::pair<ModuleLibrary, caerModuleInfo> mLoad;

	try {
		mLoad = caerLoadModuleLibrary(moduleName);
	}
	catch (const std::exception &ex) {
		boost::format exMsg = boost::format("Module '%s': %s") % moduleName % ex.what();
		libcaer::log::log(libcaer::log::logLevel::ERROR, "Module", exMsg.str().c_str());
		return;
	}

	if (mLoad.second->functions->moduleConfigInit != nullptr) {
		mLoad.second->functions->moduleConfigInit(moduleNode);
	}

	caerUnloadModuleLibrary(mLoad.first);
}

void caerModuleSM(caerModuleFunctions moduleFunctions, caerModuleData moduleData, size_t memSize,
	caerEventPacketContainer in, caerEventPacketContainer *out) {
	bool running = moduleData->running.load(std::memory_order_relaxed);

	if (moduleData->moduleStatus == CAER_MODULE_RUNNING && running) {
		if (moduleData->configUpdate.load(std::memory_order_relaxed) != 0) {
			if (moduleFunctions->moduleConfig != nullptr) {
				// Call config function, which will have to reset configUpdate.
				moduleFunctions->moduleConfig(moduleData);
			}
		}

		if (moduleFunctions->moduleRun != nullptr) {
			moduleFunctions->moduleRun(moduleData, in, out);
		}

		if (moduleData->doReset.load(std::memory_order_relaxed) != 0) {
			if (moduleFunctions->moduleReset != nullptr) {
				// Call reset function. 'doReset' variable reset is done here.
				int16_t resetCallSourceID = I16T(moduleData->doReset.exchange(0));
				moduleFunctions->moduleReset(moduleData, resetCallSourceID);
			}
		}
	}
	else if (moduleData->moduleStatus == CAER_MODULE_STOPPED && running) {
		if (memSize != 0) {
			moduleData->moduleState = calloc(1, memSize);
			if (moduleData->moduleState == nullptr) {
				return;
			}
		}
		else {
			// memSize is zero, so moduleState must be nullptr.
			moduleData->moduleState = nullptr;
		}

		if (moduleFunctions->moduleInit != nullptr) {
			if (!moduleFunctions->moduleInit(moduleData)) {
				free(moduleData->moduleState);
				moduleData->moduleState = nullptr;

				return;
			}
		}

		moduleData->moduleStatus = CAER_MODULE_RUNNING;
	}
	else if (moduleData->moduleStatus == CAER_MODULE_RUNNING && !running) {
		moduleData->moduleStatus = CAER_MODULE_STOPPED;

		if (moduleFunctions->moduleExit != nullptr) {
			moduleFunctions->moduleExit(moduleData);
		}

		free(moduleData->moduleState);
		moduleData->moduleState = nullptr;
	}
}

caerModuleData caerModuleInitialize(int16_t moduleID, const char *moduleName, sshsNode moduleNode) {
	// Allocate memory for the module.
	caerModuleData moduleData = (caerModuleData) calloc(1, sizeof(struct caer_module_data));
	if (moduleData == nullptr) {
		caerLog(CAER_LOG_ALERT, moduleName, "Failed to allocate memory for module. Error: %d.", errno);
		return (nullptr);
	}

	// Set module ID for later identification (used as quick key often).
	moduleData->moduleID = moduleID;

	// Set configuration node (so it's user accessible).
	moduleData->moduleNode = moduleNode;

	// Put module into startup state. 'running' flag is updated later based on user startup wishes.
	moduleData->moduleStatus = CAER_MODULE_STOPPED;

	// Setup default full log string name.
	size_t nameLength = strlen(moduleName);
	moduleData->moduleSubSystemString = (char *) malloc(nameLength + 1);
	if (moduleData->moduleSubSystemString == nullptr) {
		free(moduleData);

		caerLog(CAER_LOG_ALERT, moduleName, "Failed to allocate subsystem string for module.");
		return (nullptr);
	}

	strncpy(moduleData->moduleSubSystemString, moduleName, nameLength);
	moduleData->moduleSubSystemString[nameLength] = '\0';

	// Ensure static configuration is created on each module initialization.
	caerModuleConfigInit(moduleNode);

	// Per-module log level support.
	uint8_t logLevel = U8T(sshsNodeGetByte(moduleData->moduleNode, "logLevel"));

	moduleData->moduleLogLevel.store(logLevel, std::memory_order_relaxed);
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleLogLevelListener);

	// Initialize shutdown controls.
	bool runModule = sshsNodeGetBool(moduleData->moduleNode, "runAtStartup");

	sshsNodeCreateBool(moduleData->moduleNode, "running", false, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
		"Module start/stop.");
	sshsNodePutBool(moduleData->moduleNode, "running", runModule);

	moduleData->running.store(runModule, std::memory_order_relaxed);
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleShutdownListener);

	std::atomic_thread_fence(std::memory_order_release);

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

	char *newSubSystemString = (char *) malloc(subSystemStringLenght + 1);
	if (newSubSystemString == nullptr) {
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
	moduleData->configUpdate.store(0);
}

void caerModuleConfigDefaultListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData data = (caerModuleData) userData;

	// Simply set the config update flag to 1 on any attribute change.
	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		data->configUpdate.store(1);
	}
}

void caerModuleLog(caerModuleData moduleData, enum caer_log_level logLevel, const char *format, ...) {
	va_list argumentList;
	va_start(argumentList, format);
	caerLogVAFull(caerLogFileDescriptorsGetFirst(), caerLogFileDescriptorsGetSecond(),
		moduleData->moduleLogLevel.load(std::memory_order_relaxed), logLevel, moduleData->moduleSubSystemString, format,
		argumentList);
	va_end(argumentList);
}

static void caerModuleShutdownListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData data = (caerModuleData) userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		atomic_store(&data->running, changeValue.boolean);
	}
}

static void caerModuleLogLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData data = (caerModuleData) userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BYTE && caerStrEquals(changeKey, "logLevel")) {
		atomic_store(&data->moduleLogLevel, U8T(changeValue.ibyte));
	}
}

std::pair<ModuleLibrary, caerModuleInfo> caerLoadModuleLibrary(const std::string &moduleName) {
	// For each module, we search if a path exists to load it from.
	// If yes, we do so. The various OS's shared library load mechanisms
	// will keep track of reference count if same module is loaded
	// multiple times.
	boost::filesystem::path modulePath;

	{
		std::lock_guard<std::recursive_mutex> lock(glModuleData.modulePathsMutex);

		for (const auto &p : glModuleData.modulePaths) {
			if (moduleName == p.stem().string()) {
				// Found a module with same name!
				modulePath = p;
			}
		}
	}

	if (modulePath.empty()) {
		boost::format exMsg = boost::format("No module library for '%s' found.") % moduleName;
		throw std::runtime_error(exMsg.str());
	}

#if BOOST_HAS_DLL_LOAD
	ModuleLibrary moduleLibrary;
	try {
		moduleLibrary.load(modulePath.c_str(), boost::dll::load_mode::rtld_now);
	}
	catch (const std::exception &ex) {
		// Failed to load shared library!
		boost::format exMsg = boost::format("Failed to load library '%s', error: '%s'.") % modulePath.string()
			% ex.what();
		throw std::runtime_error(exMsg.str());
	}

	caerModuleInfo (*getInfo)(void);
	try {
		getInfo = moduleLibrary.get<caerModuleInfo(void)>("caerModuleGetInfo");
	}
	catch (const std::exception &ex) {
		// Failed to find symbol in shared library!
		caerUnloadModuleLibrary(moduleLibrary);
		boost::format exMsg = boost::format("Failed to find symbol in library '%s', error: '%s'.") % modulePath.string()
			% ex.what();
		throw std::runtime_error(exMsg.str());
	}
#else
	void *moduleLibrary = dlopen(modulePath.c_str(), RTLD_NOW);
	if (moduleLibrary == nullptr) {
		// Failed to load shared library!
		boost::format exMsg = boost::format("Failed to load library '%s', error: '%s'.") % modulePath.string()
		% dlerror();
		throw std::runtime_error(exMsg.str());
	}

	caerModuleInfo (*getInfo)(void) = (caerModuleInfo (*)(void)) dlsym(moduleLibrary, "caerModuleGetInfo");
	if (getInfo == nullptr) {
		// Failed to find symbol in shared library!
		caerUnloadModuleLibrary(moduleLibrary);
		boost::format exMsg = boost::format("Failed to find symbol in library '%s', error: '%s'.") % modulePath.string()
		% dlerror();
		throw std::runtime_error(exMsg.str());
	}
#endif

	caerModuleInfo info = (*getInfo)();
	if (info == nullptr) {
		caerUnloadModuleLibrary(moduleLibrary);
		boost::format exMsg = boost::format("Failed to get info from library '%s'.") % modulePath.string();
		throw std::runtime_error(exMsg.str());
	}

	return (std::pair<ModuleLibrary, caerModuleInfo>(moduleLibrary, info));
}

// Small helper to unload libraries on error.
void caerUnloadModuleLibrary(ModuleLibrary &moduleLibrary) {
#if BOOST_HAS_DLL_LOAD
	moduleLibrary.unload();
#else
	dlclose(moduleLibrary);
#endif
}

void caerUpdateModulesInformation() {
	std::lock_guard<std::recursive_mutex> lock(glModuleData.modulePathsMutex);

	sshsNode modulesNode = sshsGetNode(sshsGetGlobal(), "/caer/modules/");

	// Clear out modules information.
	sshsNodeClearSubTree(modulesNode, false);
	glModuleData.modulePaths.clear();

	// Search for available modules. Will be loaded as needed later.
	const std::string modulesSearchPath = sshsNodeGetStdString(modulesNode, "modulesSearchPath");

	// Split on '|'.
	std::vector<std::string> searchPaths;
	boost::algorithm::split(searchPaths, modulesSearchPath, boost::is_any_of("|"));

	// Search is recursive for binary shared libraries.
	const std::regex moduleRegex("\\w+\\.(so|dll|dylib)");

	for (const auto &sPath : searchPaths) {
		if (!boost::filesystem::exists(sPath)) {
			continue;
		}

		std::for_each(boost::filesystem::recursive_directory_iterator(sPath),
			boost::filesystem::recursive_directory_iterator(),
			[&moduleRegex](const boost::filesystem::directory_entry &e) {
				if (boost::filesystem::exists(e.path()) && boost::filesystem::is_regular_file(e.path()) && std::regex_match(e.path().filename().string(), moduleRegex)) {
					glModuleData.modulePaths.push_back(e.path());
				}
			});
	}

	// Sort and unique.
	vectorSortUnique(glModuleData.modulePaths);

	// No modules, cannot start!
	if (glModuleData.modulePaths.empty()) {
		boost::format exMsg = boost::format("Failed to find any modules on path(s) '%s'.") % modulesSearchPath;
		throw std::runtime_error(exMsg.str());
	}

	// Got all available modules, expose them as a sorted list.
	std::vector<std::string> modulePathsSorted;
	for (const auto &modulePath : glModuleData.modulePaths) {
		modulePathsSorted.push_back(modulePath.stem().string());
	}

	std::sort(modulePathsSorted.begin(), modulePathsSorted.end());

	std::string modulesList;
	for (const auto &modulePath : modulePathsSorted) {
		modulesList += (modulePath + ",");
	}
	modulesList.pop_back(); // Remove trailing comma.

	sshsNodeUpdateReadOnlyAttribute(modulesNode, "modulesListOptions", modulesList);

	// Now generate nodes for each of them, with their in/out information as attributes.
	for (const auto &modulePath : glModuleData.modulePaths) {
		std::string moduleName = modulePath.stem().string();

		// Load library.
		std::pair<ModuleLibrary, caerModuleInfo> mLoad;

		try {
			mLoad = caerLoadModuleLibrary(moduleName);
		}
		catch (const std::exception &ex) {
			boost::format exMsg = boost::format("Module '%s': %s") % moduleName % ex.what();
			libcaer::log::log(libcaer::log::logLevel::ERROR, "Module", exMsg.str().c_str());
			continue;
		}

		// Get SSHS node under /caer/modules/.
		sshsNode moduleNode = sshsGetRelativeNode(modulesNode, moduleName + "/");

		// Parse caerModuleInfo into SSHS.
		sshsNodeCreate(moduleNode, "version", I32T(mLoad.second->version), 0, INT32_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Module version.");
		sshsNodeCreate(moduleNode, "name", mLoad.second->name, 1, 256, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Module name.");
		sshsNodeCreate(moduleNode, "description", mLoad.second->description, 1, 8192,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Module description.");
		sshsNodeCreate(moduleNode, "type", caerModuleTypeToString(mLoad.second->type), 1, 64,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Module type.");

		if (mLoad.second->inputStreamsSize > 0) {
			sshsNode inputStreamsNode = sshsGetRelativeNode(moduleNode, "inputStreams/");

			sshsNodeCreate(inputStreamsNode, "size", I32T(mLoad.second->inputStreamsSize), 1, INT16_MAX,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of input streams.");

			for (size_t i = 0; i < mLoad.second->inputStreamsSize; i++) {
				sshsNode inputStreamNode = sshsGetRelativeNode(inputStreamsNode, std::to_string(i) + "/");
				caerEventStreamIn inputStream = &mLoad.second->inputStreams[i];

				sshsNodeCreate(inputStreamNode, "type", inputStream->type, I16T(-1), I16T(INT16_MAX),
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Input event type (-1 for any type).");
				sshsNodeCreate(inputStreamNode, "number", inputStream->number, I16T(-1), I16T(INT16_MAX),
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of inputs of this type (-1 for any number).");
				sshsNodeCreate(inputStreamNode, "readOnly", inputStream->readOnly,
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Whether this input is modified or not.");
			}
		}

		if (mLoad.second->outputStreamsSize > 0) {
			sshsNode outputStreamsNode = sshsGetRelativeNode(moduleNode, "outputStreams/");

			sshsNodeCreate(outputStreamsNode, "size", I32T(mLoad.second->outputStreamsSize), 1, INT16_MAX,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of output streams.");

			for (size_t i = 0; i < mLoad.second->outputStreamsSize; i++) {
				sshsNode outputStreamNode = sshsGetRelativeNode(outputStreamsNode, std::to_string(i) + "/");
				caerEventStreamOut outputStream = &mLoad.second->outputStreams[i];

				sshsNodeCreate(outputStreamNode, "type", outputStream->type, I16T(-1), I16T(INT16_MAX),
					SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
					"Output event type (-1 for undefined output determined at runtime).");
			}
		}

		// Done, unload library.
		caerUnloadModuleLibrary(mLoad.first);
	}
}
