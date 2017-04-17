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
#include <boost/range/join.hpp>
#include <dlfcn.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <libcaercpp/libcaer.hpp>
using namespace libcaer::log;

struct ModuleInfo;

struct ModuleConnection {
	ModuleInfo *otherModule;
	bool copyNeeded;
};

struct ModuleConnectivity {
	int16_t typeId;
	std::vector<ModuleConnection> connections;

	ModuleConnectivity(int16_t t) {
		typeId = t;
	}

	// Comparison operators.
	bool operator==(const ModuleConnectivity &rhs) const noexcept {
		return (typeId == rhs.typeId);
	}

	bool operator!=(const ModuleConnectivity &rhs) const noexcept {
		return (typeId != rhs.typeId);
	}

	bool operator<(const ModuleConnectivity &rhs) const noexcept {
		return (typeId < rhs.typeId);
	}

	bool operator>(const ModuleConnectivity &rhs) const noexcept {
		return (typeId > rhs.typeId);
	}

	bool operator<=(const ModuleConnectivity &rhs) const noexcept {
		return (typeId <= rhs.typeId);
	}

	bool operator>=(const ModuleConnectivity &rhs) const noexcept {
		return (typeId >= rhs.typeId);
	}
};

struct OrderedInput {
	int16_t typeId;
	int16_t afterModuleId;

	OrderedInput(int16_t t, int16_t m) {
		typeId = t;
		afterModuleId = m;
	}

	// Comparison operators.
	bool operator==(const OrderedInput &rhs) const noexcept {
		return (typeId == rhs.typeId);
	}

	bool operator!=(const OrderedInput &rhs) const noexcept {
		return (typeId != rhs.typeId);
	}

	bool operator<(const OrderedInput &rhs) const noexcept {
		return (typeId < rhs.typeId);
	}

	bool operator>(const OrderedInput &rhs) const noexcept {
		return (typeId > rhs.typeId);
	}

	bool operator<=(const OrderedInput &rhs) const noexcept {
		return (typeId <= rhs.typeId);
	}

	bool operator>=(const OrderedInput &rhs) const noexcept {
		return (typeId >= rhs.typeId);
	}
};

struct ModuleInfo {
	// Module identification.
	int16_t id;
	std::string name;
	// SSHS configuration node.
	sshsNode configNode;
	// Parsed moduleInput configuration.
	std::unordered_map<int16_t, std::vector<OrderedInput>> inputDefinition;
	// Connectivity graph (I/O).
	bool IODone;
	std::vector<ModuleConnectivity> inputs;
	std::vector<ModuleConnectivity> outputs;
	// Loadable module support.
	std::string library;
	void *libraryHandle;
	caerModuleInfo libraryInfo;
};

struct ActiveStreams {
	int16_t sourceId;
	int16_t typeId;
};

static struct {
	sshsNode configNode;
	atomic_bool systemRunning;
	atomic_bool running;
	atomic_uint_fast32_t dataAvailable;
	std::unordered_map<int16_t, ModuleInfo> modules;
} glMainloopData;

static std::vector<boost::filesystem::path> modulePaths;

static int caerMainloopRunner(void);
static void caerMainloopSignalHandler(int signal);
static void caerMainloopSystemRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
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
			if (boost::filesystem::exists(e.path()) && boost::filesystem::is_regular_file(e.path()) && std::regex_match(e.path().filename().string(), moduleRegex)) {
				modulePaths.push_back(e.path());
			}
		});

	// Sort and unique.
	std::sort(modulePaths.begin(), modulePaths.end());
	modulePaths.erase(std::unique(modulePaths.begin(), modulePaths.end()), modulePaths.end());

	// No modules, cannot start!
	if (modulePaths.empty()) {
		log(logLevel::CRITICAL, "Mainloop", "Failed to find any modules on path '%s'.", moduleSearchPath.c_str());
		return;
	}

	// No data at start-up.
	glMainloopData.dataAvailable.store(0);

	// System running control, separate to allow mainloop stop/start.
	glMainloopData.systemRunning.store(true);

	sshsNode systemNode = sshsGetNode(sshsGetGlobal(), "/caer/");
	sshsNodeCreateBool(systemNode, "running", true, SSHS_FLAGS_NORMAL);
	sshsNodeAddAttributeListener(systemNode, NULL, &caerMainloopSystemRunningListener);

	// Mainloop running control.
	glMainloopData.running.store(true);

	glMainloopData.configNode = sshsGetNode(sshsGetGlobal(), "/");
	sshsNodeCreateBool(glMainloopData.configNode, "running", true, SSHS_FLAGS_NORMAL);
	sshsNodeAddAttributeListener(glMainloopData.configNode, NULL, &caerMainloopRunningListener);

	while (glMainloopData.systemRunning.load()) {
		if (!glMainloopData.running.load()) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			continue;
		}

		int result = caerMainloopRunner();

		// On failure, make sure to disable mainloop, user will have to fix it.
		if (result == EXIT_FAILURE) {
			sshsNodePutBool(glMainloopData.configNode, "running", false);

			log(logLevel::CRITICAL, "Mainloop",
				"Failed to start mainloop, please fix the configuration and try again!");
		}
	}
}

static bool checkInputOutputStreamDefinitions(caerModuleInfo info) {
	if (info->type == CAER_MODULE_INPUT) {
		if (info->inputStreams != NULL || info->inputStreamsSize != 0 || info->outputStreams == NULL
			|| info->outputStreamsSize == 0) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Wrong I/O event stream definitions for type INPUT.",
				info->name);
			return (false);
		}
	}
	else if (info->type == CAER_MODULE_OUTPUT) {
		if (info->inputStreams == NULL || info->inputStreamsSize == 0 || info->outputStreams != NULL
			|| info->outputStreamsSize != 0) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Wrong I/O event stream definitions for type OUTPUT.",
				info->name);
			return (false);
		}

		// Also ensure that all input streams of an output module are marked read-only.
		bool readOnlyError = false;

		for (size_t i = 0; i < info->inputStreamsSize; i++) {
			if (!info->inputStreams[i].readOnly) {
				readOnlyError = true;
				break;
			}
		}

		if (readOnlyError) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Input event streams not marked read-only for type OUTPUT.",
				info->name);
			return (false);
		}
	}
	else {
		// CAER_MODULE_PROCESSOR
		if (info->inputStreams == NULL || info->inputStreamsSize == 0) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Wrong I/O event stream definitions for type PROCESSOR.",
				info->name);
			return (false);
		}

		// If no output streams are defined, then at least one input event
		// stream must not be readOnly, so that there is modified data to output.
		if (info->outputStreams == NULL || info->outputStreamsSize == 0) {
			bool readOnlyError = true;

			for (size_t i = 0; i < info->inputStreamsSize; i++) {
				if (!info->inputStreams[i].readOnly) {
					readOnlyError = false;
					break;
				}
			}

			if (readOnlyError) {
				log(logLevel::ERROR, "Mainloop",
					"Module '%s': No output streams and all input streams are marked read-only for type PROCESSOR.",
					info->name);
				return (false);
			}
		}
	}

	return (true);
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * Number must be either -1 or well defined (1-INT16_MAX). Zero not allowed.
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then number must also be -1; having a defined
 * number in this case makes no sense (N of any type???), a special exception
 * is made for the number 1 (1 of any type) with inputs, which can be useful.
 * Also this must then be the only definition.
 * If number is -1, then either the type is also -1 and this is the
 * only event stream definition (same as rule above), OR the type is well
 * defined and this is the only event stream definition for that type.
 */
static bool checkInputStreamDefinitions(caerEventStreamIn inputStreams, size_t inputStreamsSize, const char *name) {
	for (size_t i = 0; i < inputStreamsSize; i++) {
		// Check type range.
		if (inputStreams[i].type < -1) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Input stream has invalid type value.", name);
			return (false);
		}

		// Check number range.
		if (inputStreams[i].number < -1 || inputStreams[i].number == 0) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Input stream has invalid number value.", name);
			return (false);
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && inputStreams[i - 1].type >= inputStreams[i].type) {
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': Input stream has invalid order of declaration or duplicates.", name);
			return (false);
		}

		// Check that any type is always together with any number or 1, and the
		// only definition present in that case.
		if (inputStreams[i].type == -1
			&& ((inputStreams[i].number != -1 && inputStreams[i].number != 1) || inputStreamsSize != 1)) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Input stream has invalid any declaration.", name);
			return (false);
		}
	}

	return (true);
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then this must then be the only definition.
 */
static bool checkOutputStreamDefinitions(caerEventStreamOut outputStreams, size_t outputStreamsSize, const char *name) {
	// If type is any, must be the only definition.
	if (outputStreamsSize == 1 && outputStreams[0].type == -1) {
		return (true);
	}

	for (size_t i = 0; i < outputStreamsSize; i++) {
		// Check type range.
		if (outputStreams[i].type < 0) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Output stream has invalid type value.", name);
			return (false);
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && outputStreams[i - 1].type >= outputStreams[i].type) {
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': Output stream has invalid order of declaration or duplicates.", name);
			return (false);
		}
	}

	return (true);
}

/**
 * Check for the presence of the 'moduleInput' and 'moduleOutput' configuration
 * parameters, depending on the type of module and its requirements.
 */
static bool checkModuleInputOutput(caerModuleInfo info, sshsNode configNode) {
	if (info->type == CAER_MODULE_INPUT) {
		// moduleInput must not exist for INPUT modules.
		if (sshsNodeAttributeExists(configNode, "moduleInput", SSHS_STRING)) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': INPUT type cannot have a 'moduleInput' attribute.",
				info->name);
			return (false);
		}
	}
	else {
		// CAER_MODULE_OUTPUT / CAER_MODULE_PROCESSOR
		// moduleInput must exist for OUTPUT and PROCESSOR modules.
		if (!sshsNodeAttributeExists(configNode, "moduleInput", SSHS_STRING)) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': OUTPUT/PROCESSOR types must have a 'moduleInput' attribute.",
				info->name);
			return (false);
		}
	}

	if (info->type == CAER_MODULE_OUTPUT) {
		// moduleOutput must not exist for OUTPUT modules.
		if (sshsNodeAttributeExists(configNode, "moduleOutput", SSHS_STRING)) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': OUTPUT type cannot have a 'moduleOutput' attribute.",
				info->name);
			return (false);
		}
	}
	else {
		// CAER_MODULE_INPUT / CAER_MODULE_PROCESSOR
		// moduleOutput must exist for INPUT and PROCESSOR modules, only
		// if their outputs are undefined (-1).
		if (info->outputStreams != NULL && info->outputStreamsSize == 1 && info->outputStreams[0].type == -1
			&& !sshsNodeAttributeExists(configNode, "moduleOutput", SSHS_STRING)) {
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': INPUT/PROCESSOR types with ANY_TYPE definition must have a 'moduleOutput' attribute.",
				info->name);
			return (false);
		}
	}

	return (true);
}

static std::vector<int16_t> parseTypeIDString(const std::string &types) {
	// Empty string, cannot be!
	if (types.empty()) {
		throw std::length_error("Empty Type ID string.");
	}

	std::vector<int16_t> results;

	// Extract all type IDs from comma-separated string.
	std::stringstream typesStream(types);
	std::string typeString;

	while (std::getline(typesStream, typeString, ',')) {
		int type = std::stoi(typeString);

		// Check type ID value.
		if (type < 0 || type > INT16_MAX) {
			throw std::out_of_range("Type ID negative or too big.");
		}

		// Add extracted Type IDs to the result vector.
		results.push_back(static_cast<int16_t>(type));
	}

	// Ensure that something was extracted.
	if (results.empty()) {
		throw std::length_error("Empty extracted Type ID vector.");
	}

	// Detect duplicates.
	size_t sizeBefore = results.size();

	std::sort(results.begin(), results.end());
	results.erase(std::unique(results.begin(), results.end()), results.end());

	size_t sizeAfter = results.size();

	// If size changed, duplicates must have been removed, so they existed
	// in the first place, which is not allowed.
	if (sizeAfter != sizeBefore) {
		throw std::invalid_argument("Duplicate Type ID found.");
	}

	return (results);
}

static std::vector<OrderedInput> parseAugmentedTypeIDString(const std::string &types) {
	// Empty string, cannot be!
	if (types.empty()) {
		throw std::length_error("Empty Augmented Type ID string.");
	}

	std::vector<OrderedInput> results;

	// Extract all type IDs from comma-separated string.
	std::stringstream typesStream(types);
	std::string typeString;

	while (std::getline(typesStream, typeString, ',')) {
		size_t modifierPosition = 0;
		int type = std::stoi(typeString, &modifierPosition);

		// Check type ID value.
		if (type < 0 || type > INT16_MAX) {
			throw std::out_of_range("Type ID negative or too big.");
		}

		int afterModuleOrder = -1;

		if (modifierPosition != typeString.length() && typeString.at(modifierPosition) == 'a') {
			std::string orderString = typeString.substr(modifierPosition + 1);

			afterModuleOrder = std::stoi(orderString);

			// Check Module ID value.
			if (afterModuleOrder < 0 || afterModuleOrder > INT16_MAX) {
				throw std::out_of_range("Module ID negative or too big.");
			}
		}

		// Add extracted Type IDs to the result vector.
		results.push_back(OrderedInput(static_cast<int16_t>(type), static_cast<int16_t>(afterModuleOrder)));
	}

	// Ensure that something was extracted.
	if (results.empty()) {
		throw std::length_error("Empty extracted Augmented Type ID vector.");
	}

	// Detect duplicates.
	size_t sizeBefore = results.size();

	std::sort(results.begin(), results.end());
	results.erase(std::unique(results.begin(), results.end()), results.end());

	size_t sizeAfter = results.size();

	// If size changed, duplicates must have been removed, so they existed
	// in the first place, which is not allowed.
	if (sizeAfter != sizeBefore) {
		throw std::invalid_argument("Duplicate Type ID found.");
	}

	return (results);
}

/**
 * moduleInput strings have the following format: different input IDs are
 * separated by a white-space character, for each input ID the used input
 * types are listed inside square-brackets [] and separated by a comma.
 * For example: "1[1,2,3] 2[2] 4[1,2]" means the inputs are: types 1,2,3
 * from module 1, type 2 from module 2, and types 1,2 from module 4.
 */
static bool parseModuleInput(const std::string &inputDefinition,
	std::unordered_map<int16_t, std::vector<OrderedInput>> &resultMap, const char *name) {
	// Empty string, cannot be!
	if (inputDefinition.empty()) {
		return (false);
	}

	try {
		std::regex wsRegex("\\s+"); // Whitespace(s) Regex.
		auto iter = std::sregex_token_iterator(inputDefinition.begin(), inputDefinition.end(), wsRegex, -1);

		while (iter != std::sregex_token_iterator()) {
			std::regex typeRegex("(\\d+)\\[(\\w+(?:,\\w+)*)\\]"); // Single Input Definition Regex.
			std::smatch matches;
			std::regex_match(iter->first, iter->second, matches, typeRegex);

			// Did we find the expected matches?
			if (matches.size() != 3) {
				throw std::length_error("Malformed input definition.");
			}

			// Get referenced module ID first.
			std::string idString = matches[1];
			int id = std::stoi(idString);

			// Check module ID value.
			if (id < 0 || id > INT16_MAX) {
				throw std::out_of_range("Referenced module ID negative or too big.");
			}

			// If this module ID already exists in the map, it means there are
			// multiple definitions for the same ID; this is not allowed!
			if (resultMap.count(static_cast<int16_t>(id)) == 1) {
				throw std::out_of_range("Duplicate referenced module ID found.");
			}

			// Check that the referenced module ID actually exists in the system.
			if (glMainloopData.modules.count(static_cast<int16_t>(id)) == 0) {
				throw std::out_of_range("Unknown referenced module ID found.");
			}

			// Then get the various type IDs for that module.
			std::string typeString = matches[2];

			resultMap[static_cast<int16_t>(id)] = parseAugmentedTypeIDString(typeString);

			iter++;
		}

		// inputDefinition was not empty, but we didn't manage to parse anything.
		if (resultMap.empty()) {
			throw std::length_error("Invalid input definition.");
		}
	}
	catch (std::logic_error &e) {
		// Clean map of any partial results on failure.
		resultMap.clear();

		log(logLevel::ERROR, "Mainloop", "Module '%s': Invalid 'moduleInput' configuration: %s", name, e.what());

		return (false);
	}

	return (true);
}

static bool checkInputDefinitionAgainstEventStreamIn(
	std::unordered_map<int16_t, std::vector<OrderedInput>> &inputDefinition, caerEventStreamIn eventStreams,
	size_t eventStreamsSize, const char *name) {
	// Use parsed moduleInput configuration to get per-type count.
	std::unordered_map<int, int> typeCount;

	for (auto &in : inputDefinition) {
		for (auto typeAndOrder : in.second) {
			typeCount[typeAndOrder.typeId]++;
		}
	}

	// Any_Type/Any_Number means there just needs to be something.
	if (eventStreamsSize == 1 && eventStreams[0].type == -1 && eventStreams[0].number == -1) {
		if (typeCount.empty()) {
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': ANY_TYPE/ANY_NUMBER definition has no connected input streams.", name);
			return (false);
		}

		return (true);
	}

	// Any_Type/1 means there must be exactly one type with count of 1.
	if (eventStreamsSize == 1 && eventStreams[0].type == -1 && eventStreams[0].number == 1) {
		if (typeCount.size() != 1 || typeCount.cbegin()->second != 1) {
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': ANY_TYPE/1 definition requires 1 connected input stream of some type.", name);
			return (false);
		}

		return (true);
	}

	// All other cases involve possibly multiple definitions with a defined type.
	// Since EventStreamIn definitions are strictly monotonic in this case, we
	// first check that the number of definitions and counted types match.
	if (typeCount.size() != eventStreamsSize) {
		log(logLevel::ERROR, "Mainloop",
			"Module '%s': DEFINED_TYPE definitions require as many connected types as given.", name);
		return (false);
	}

	for (size_t i = 0; i < eventStreamsSize; i++) {
		// Defined_Type/Any_Number means there must be 1 or more such types present.
		if (eventStreams[i].type >= 0 && eventStreams[i].number == -1) {
			if (typeCount[eventStreams[i].type] < 1) {
				log(logLevel::ERROR, "Mainloop",
					"Module '%s': DEFINED_TYPE/ANY_NUMBER definition requires at least one connected input stream of that type.",
					name);
				return (false);
			}
		}

		// Defined_Type/Defined_Number means there must be exactly as many such types present.
		if (eventStreams[i].type >= 0 && eventStreams[i].number > 0) {
			if (typeCount[eventStreams[i].type] != eventStreams[i].number) {
				log(logLevel::ERROR, "Mainloop",
					"Module '%s': DEFINED_TYPE/DEFINED_NUMBER definition requires exactly that many connected input streams of that type.",
					name);
				return (false);
			}
		}
	}

	return (true);
}

/**
 * Input modules _must_ have all their outputs well defined, or it becomes impossible
 * to validate and build the follow-up chain of processors and outputs correctly.
 * Now, this may not always be the case, for example File Input modules don't know a-priori
 * what their outputs are going to be (so they're declared with type set to -1).
 * For those cases, we need additional information, which we get from the 'moduleOutput'
 * configuration parameter that is required to be set in this case. For other input modules,
 * where the outputs are well known, like devices, this must not be set.
 */
static bool parseModuleOutput(const std::string &moduleOutput, std::vector<ModuleConnectivity> &outputs) {
	std::vector<int16_t> results;

	try {
		results = parseTypeIDString(moduleOutput);
	}
	catch (...) {
		return (false);
	}

	for (auto type : results) {
		outputs.push_back(ModuleConnectivity(type));
	}

	return (true);
}

static bool parseEventStreamOutDefinition(caerEventStreamOut eventStreams, size_t eventStreamsSize,
	std::vector<ModuleConnectivity> &outputs) {
	for (size_t i = 0; i < eventStreamsSize; i++) {
		outputs.push_back(ModuleConnectivity(eventStreams[i].type));
	}

	return (true);
}

static int caerMainloopRunner(void) {
	// At this point configuration is already loaded, so let's see if everything
	// we need to build and run a mainloop is really there.
	// Each node in the root / is a module, with a short-name as node-name,
	// an ID (16-bit integer, "moduleId") as attribute, and the module's library
	// (string, "moduleLibrary") as attribute.
	size_t modulesSize = 0;
	sshsNode *modules = sshsNodeGetChildren(glMainloopData.configNode, &modulesSize);
	if (modules == NULL || modulesSize == 0) {
		// Empty configuration.
		log(logLevel::ERROR, "Mainloop", "No module configuration found.");
		return (EXIT_FAILURE);
	}

	for (size_t i = 0; i < modulesSize; i++) {
		sshsNode module = modules[i];
		std::string moduleName = sshsNodeGetName(module);

		if (moduleName == "caer") {
			// Skip system configuration, not a module.
			continue;
		}

		if (!sshsNodeAttributeExists(module, "moduleId", SSHS_SHORT)
			|| !sshsNodeAttributeExists(module, "moduleLibrary", SSHS_STRING)) {
			// Missing required attributes, notify and skip.
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': Configuration is missing core attributes 'moduleId' and/or 'moduleLibrary'.",
				moduleName.c_str());
			continue;
		}

		ModuleInfo info = { };
		info.id = sshsNodeGetShort(module, "moduleId");
		info.name = moduleName;
		info.configNode = module;

		char *moduleLibrary = sshsNodeGetString(module, "moduleLibrary");
		info.library = moduleLibrary;
		free(moduleLibrary);

		// Put data into an unordered map that holds all valid modules.
		// This also ensure the numerical ID is unique!
		auto result = glMainloopData.modules.insert(std::make_pair(info.id, info));
		if (!result.second) {
			// Failed insertion, key (ID) already exists!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Module with ID %d already exists.", moduleName.c_str(),
				info.id);
			continue;
		}
	}

	// Free temporary result nodes array.
	free(modules);

	// At this point we have a map with all the valid modules and their info.
	// If that map is empty, there was nothing valid present.
	if (glMainloopData.modules.empty()) {
		log(logLevel::ERROR, "Mainloop", "No valid modules configuration found.");
		return (EXIT_FAILURE);
	}
	else {
		log(logLevel::NOTICE, "Mainloop", "%d modules found.", glMainloopData.modules.size());
	}

	// Let's load the module libraries and get their internal info.
	for (auto &m : glMainloopData.modules) {
		// For each module, we search if a path exists to load it from.
		// If yes, we do so. The various OS's shared library load mechanisms
		// will keep track of reference count if same module is loaded
		// multiple times.
		boost::filesystem::path modulePath;

		for (auto &p : modulePaths) {
			if (m.second.library == p.stem().string()) {
				// Found a module with same name!
				modulePath = p;
			}
		}

		if (modulePath.empty()) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': No module library '%s' found.", m.second.name.c_str(),
				m.second.library.c_str());
			continue;
		}

		log(logLevel::NOTICE, "Mainloop", "Module '%s': Loading module library '%s'.", m.second.name.c_str(),
			modulePath.c_str());

		// TODO: Windows support.
		void *moduleLibrary = dlopen(modulePath.c_str(), RTLD_NOW);
		if (moduleLibrary == NULL) {
			// Failed to load shared library!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to load library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), dlerror());
			continue;
		}

		caerModuleInfo (*getInfo)(void) = (caerModuleInfo (*)(void)) dlsym(moduleLibrary, "caerModuleGetInfo");
		if (getInfo == NULL) {
			// Failed to find symbol in shared library!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to find symbol in library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), dlerror());
			dlclose(moduleLibrary);
			continue;
		}

		caerModuleInfo info = (*getInfo)();
		if (info == NULL) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to get info from library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), dlerror());
			dlclose(moduleLibrary);
			continue;
		}

		// Check that the modules respect the basic I/O definition requirements.
		if (!checkInputOutputStreamDefinitions(info)) {
			dlclose(moduleLibrary);
			continue;
		}

		// Check I/O event stream definitions for correctness.
		if (info->inputStreams != NULL) {
			if (!checkInputStreamDefinitions(info->inputStreams, info->inputStreamsSize, info->name)) {
				dlclose(moduleLibrary);
				continue;
			}
		}

		if (info->outputStreams != NULL) {
			if (!checkOutputStreamDefinitions(info->outputStreams, info->outputStreamsSize, info->name)) {
				dlclose(moduleLibrary);
				continue;
			}
		}

		if (!checkModuleInputOutput(info, m.second.configNode)) {
			dlclose(moduleLibrary);
			continue;
		}

		m.second.libraryHandle = moduleLibrary;
		m.second.libraryInfo = info;
	}

	// If any modules failed to load, exit program now. We didn't do that before, so that we
	// could run through all modules and check them all in one go.
	for (auto &m : glMainloopData.modules) {
		if (m.second.libraryHandle == NULL || m.second.libraryInfo == NULL) {
			// Clean up generated data on failure.
			glMainloopData.modules.clear();

			log(logLevel::ERROR, "Mainloop", "Errors in module library loading, terminating mainloop now.");

			return (EXIT_FAILURE);
		}
	}

	std::vector<std::reference_wrapper<ModuleInfo>> inputModules;
	std::vector<std::reference_wrapper<ModuleInfo>> outputModules;
	std::vector<std::reference_wrapper<ModuleInfo>> processorModules;

	// Now we must parse, validate and create the connectivity map between modules.
	// First we sort the modules into their three possible categories.
	for (auto &m : glMainloopData.modules) {
		if (m.second.libraryInfo->type == CAER_MODULE_INPUT) {
			inputModules.push_back(m.second);
		}
		else if (m.second.libraryInfo->type == CAER_MODULE_OUTPUT) {
			outputModules.push_back(m.second);
		}
		else {
			processorModules.push_back(m.second);
		}
	}

	// Then we parse all the 'moduleInput' configurations for OUTPUT and
	// PROCESSOR modules, ...
	for (auto &m : boost::join(outputModules, processorModules)) {
		char *moduleInput = sshsNodeGetString(m.get().configNode, "moduleInput");
		std::string inputDefinition = moduleInput;
		free(moduleInput);

		if (!parseModuleInput(inputDefinition, m.get().inputDefinition, m.get().name.c_str())) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to parse 'moduleInput' configuration.",
				m.get().name.c_str());

			// Clean up generated data on failure.
			glMainloopData.modules.clear();

			return (EXIT_FAILURE);
		}

		if (!checkInputDefinitionAgainstEventStreamIn(m.get().inputDefinition, m.get().libraryInfo->inputStreams,
			m.get().libraryInfo->inputStreamsSize, m.get().name.c_str())) {
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': Failed to verify 'moduleInput' configuration against input event stream definitions.",
				m.get().name.c_str());

			// Clean up generated data on failure.
			glMainloopData.modules.clear();

			return (EXIT_FAILURE);
		}
	}

	// ... as well as the 'moduleOutput' configurations for certain INPUT
	// and PROCESSOR modules that have an ANY type declaration. If the types
	// are instead well defined, we parse the event stream definition directly.
	for (auto &m : boost::join(inputModules, processorModules)) {
		caerModuleInfo info = m.get().libraryInfo;

		// ANY type declaration.
		if (info->outputStreams != NULL && info->outputStreamsSize == 1 && info->outputStreams[0].type == -1) {
			char *moduleOutput = sshsNodeGetString(m.get().configNode, "moduleOutput");
			std::string outputDefinition = moduleOutput;
			free(moduleOutput);

			if (!parseModuleOutput(outputDefinition, m.get().outputs)) {
				log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to parse 'moduleOutput' configuration.",
					m.get().name.c_str());

				// Clean up generated data on failure.
				glMainloopData.modules.clear();

				return (EXIT_FAILURE);
			}
		}
		else if (info->outputStreams != NULL) {
			if (!parseEventStreamOutDefinition(info->outputStreams, info->outputStreamsSize, m.get().outputs)) {
				log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to parse output event stream configuration.",
					m.get().name.c_str());

				// Clean up generated data on failure.
				glMainloopData.modules.clear();

				return (EXIT_FAILURE);
			}
		}
	}

	// There's multiple ways now to build the full connectivity graph once we
	// have all the starting points. Simple and efficient is to take an input
	// and follow along until either we've reached all the end points (outputs)
	// that are reachable, or until we need some other input, at which point we
	// stop and continue with the next input, until we've processed all inputs,
	// and then necessarily resolved the whole connected graph. Whatever inputs
	// remain with no outgoing connection, or whatever processors and outputs
	// have no incoming connections reaching them, can then be pruned as invalid.
	for (auto &m : inputModules) {
		int16_t currInputModuleId = m.get().id;

	}

//	for (auto iter = processorModules.begin(); iter != processorModules.end();) {
//		bool unconnected = false;
//
//		if (unconnected) {
//			iter = processorModules.erase(iter);
//		}
//		else {
//			++iter;
//		}
//	}

	for (auto m : glMainloopData.modules) {
		std::cout << m.second.id << "-MOD:" << m.second.libraryInfo->type << "-" << m.second.name << std::endl;

		for (auto i : m.second.inputs) {
			std::cout << " -->" << i.typeId << "-IN" << std::endl;
		}

		for (auto o : m.second.outputs) {
			std::cout << " -->" << o.typeId << "-OUT" << std::endl;
		}
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
//	// After each successful main-loop run, free the memory that was
//	// accumulated for things like packets, valid only during the run.
//	struct genericFree *memFree = NULL;
//	while ((memFree = (struct genericFree *) utarray_next(glMainloopData.memoryToFree, memFree)) != NULL) {
//		memFree->func(memFree->memPtr);
//	}
//	utarray_clear(glMainloopData.memoryToFree);
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

	// If no data is available, sleep for a millisecond to avoid wasting resources.
	// Wait for someone to toggle the module shutdown flag OR for the loop
	// itself to signal termination.
	size_t sleepCount = 0;

	while (glMainloopData.running.load(std::memory_order_relaxed)) {
		// Run only if data available to consume, else sleep. But make a run
		// anyway each second, to detect new devices for example.
		if (glMainloopData.dataAvailable.load(std::memory_order_acquire) > 0 || sleepCount > 1000) {
			sleepCount = 0;

			// TODO: execute modules.
		}
		else {
			sleepCount++;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Cleanup modules.
	glMainloopData.modules.clear();

	return (EXIT_SUCCESS);
}

void caerMainloopDataNotifyIncrease(void *p) {
	UNUSED_ARGUMENT(p);

	glMainloopData.dataAvailable.fetch_add(1, std::memory_order_release);
}

void caerMainloopDataNotifyDecrease(void *p) {
	UNUSED_ARGUMENT(p);

	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	glMainloopData.dataAvailable.fetch_sub(1, std::memory_order_relaxed);
}

// Only use this inside the mainloop-thread, not inside any other thread,
// like additional data acquisition threads or output threads.
void caerMainloopFreeAfterLoop(void (*func)(void *mem), void *memPtr) {

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

	// Simply set all the running flags to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	glMainloopData.systemRunning.store(false);
	glMainloopData.running.store(false);
}

static void caerMainloopSystemRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(userData);
	UNUSED_ARGUMENT(changeValue);

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		glMainloopData.systemRunning.store(false);
		glMainloopData.running.store(false);
	}
}

static void caerMainloopRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(userData);

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		glMainloopData.running.store(changeValue.boolean);
	}
}
