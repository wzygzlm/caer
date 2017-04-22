#include "mainloop.h"
#include <csignal>
#include <climits>
#include <unistd.h>
#include <dlfcn.h>

#include <string>
#include <regex>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/range/join.hpp>
#include <boost/format.hpp>

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
	const std::string name;
	// SSHS configuration node.
	sshsNode configNode;
	// Parsed moduleInput configuration.
	std::unordered_map<int16_t, std::vector<OrderedInput>> inputDefinition;
	// Connectivity graph (I/O).
	bool IODone;
	std::vector<ModuleConnectivity> inputs;
	std::vector<ModuleConnectivity> outputs;
	// Loadable module support.
	const std::string library;
	void *libraryHandle;
	caerModuleInfo libraryInfo;

	ModuleInfo() :
			id(-1),
			name(),
			configNode(NULL),
			IODone(false),
			library(),
			libraryHandle(NULL),
			libraryInfo(NULL) {
	}

	ModuleInfo(int16_t i, const std::string &n, sshsNode c, const std::string &l) :
			id(i),
			name(n),
			configNode(c),
			IODone(false),
			library(l),
			libraryHandle(NULL),
			libraryInfo(NULL) {
	}
};

struct DependencyNode {
	int16_t id;
	size_t depth;
	std::vector<DependencyNode> *parent;
	std::shared_ptr<std::vector<DependencyNode>> next;
};

struct ActiveStreams {
	int16_t sourceId;
	int16_t typeId;
	bool isProcessor;
	std::vector<int16_t> users;
	std::shared_ptr<std::vector<DependencyNode>> dependencies;

	ActiveStreams(int16_t s, int16_t t) {
		sourceId = s;
		typeId = t;
		isProcessor = false;
	}

	// Comparison operators.
	bool operator==(const ActiveStreams &rhs) const noexcept {
		return (sourceId == rhs.sourceId && typeId == rhs.typeId);
	}

	bool operator!=(const ActiveStreams &rhs) const noexcept {
		return (sourceId != rhs.sourceId || typeId != rhs.typeId);
	}

	bool operator<(const ActiveStreams &rhs) const noexcept {
		return (sourceId < rhs.sourceId || (sourceId == rhs.sourceId && typeId < rhs.typeId));
	}

	bool operator>(const ActiveStreams &rhs) const noexcept {
		return (sourceId > rhs.sourceId || (sourceId == rhs.sourceId && typeId > rhs.typeId));
	}

	bool operator<=(const ActiveStreams &rhs) const noexcept {
		return (sourceId < rhs.sourceId || (sourceId == rhs.sourceId && typeId <= rhs.typeId));
	}

	bool operator>=(const ActiveStreams &rhs) const noexcept {
		return (sourceId > rhs.sourceId || (sourceId == rhs.sourceId && typeId >= rhs.typeId));
	}
};

static struct {
	sshsNode configNode;
	atomic_bool systemRunning;
	atomic_bool running;
	atomic_uint_fast32_t dataAvailable;
	std::unordered_map<int16_t, ModuleInfo> modules;
	std::vector<ActiveStreams> streams;
	std::vector<std::reference_wrapper<ModuleInfo>> globalExecution;
} glMainloopData;

static std::vector<boost::filesystem::path> modulePaths;

static int caerMainloopRunner(void);
static void caerMainloopSignalHandler(int signal);
static void caerMainloopSystemRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void caerMainloopRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

template<class T>
static void vectorSortUnique(std::vector<T> &vec) {
	std::sort(vec.begin(), vec.end());
	vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

template<class T>
static bool vectorDetectDuplicates(std::vector<T> &vec) {
	// Detect duplicates.
	size_t sizeBefore = vec.size();

	vectorSortUnique(vec);

	size_t sizeAfter = vec.size();

	// If size changed, duplicates must have been removed, so they existed
	// in the first place!
	if (sizeAfter != sizeBefore) {
		return (true);
	}

	return (false);
}

void caerMainloopRun(void) {
	// Install signal handler for global shutdown.
#if defined(OS_WINDOWS)
	if (signal(SIGTERM, &caerMainloopSignalHandler) == SIG_ERR) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (signal(SIGINT, &caerMainloopSignalHandler) == SIG_ERR) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (signal(SIGBREAK, &caerMainloopSignalHandler) == SIG_ERR) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGBREAK. Error: %d.", errno);
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
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdown, NULL) == -1) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGINT. Error: %d.", errno);
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
	const std::string moduleSearchPath = moduleSearchPathC;
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
	vectorSortUnique(modulePaths);

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

static void checkInputOutputStreamDefinitions(caerModuleInfo info) {
	if (info->type == CAER_MODULE_INPUT) {
		if (info->inputStreams != NULL || info->inputStreamsSize != 0 || info->outputStreams == NULL
			|| info->outputStreamsSize == 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type INPUT.");
		}
	}
	else if (info->type == CAER_MODULE_OUTPUT) {
		if (info->inputStreams == NULL || info->inputStreamsSize == 0 || info->outputStreams != NULL
			|| info->outputStreamsSize != 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type OUTPUT.");
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
			throw std::domain_error("Input event streams not marked read-only for type OUTPUT.");
		}
	}
	else {
		// CAER_MODULE_PROCESSOR
		if (info->inputStreams == NULL || info->inputStreamsSize == 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type PROCESSOR.");
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
				throw std::domain_error(
					"No output streams and all input streams are marked read-only for type PROCESSOR.");
			}
		}
	}
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
static void checkInputStreamDefinitions(caerEventStreamIn inputStreams, size_t inputStreamsSize) {
	for (size_t i = 0; i < inputStreamsSize; i++) {
		// Check type range.
		if (inputStreams[i].type < -1) {
			throw std::domain_error("Input stream has invalid type value.");
		}

		// Check number range.
		if (inputStreams[i].number < -1 || inputStreams[i].number == 0) {
			throw std::domain_error("Input stream has invalid number value.");
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && inputStreams[i - 1].type >= inputStreams[i].type) {
			throw std::domain_error("Input stream has invalid order of declaration or duplicates.");
		}

		// Check that any type is always together with any number or 1, and the
		// only definition present in that case.
		if (inputStreams[i].type == -1
			&& ((inputStreams[i].number != -1 && inputStreams[i].number != 1) || inputStreamsSize != 1)) {
			throw std::domain_error("Input stream has invalid any declaration.");
		}
	}
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then this must then be the only definition.
 */
static void checkOutputStreamDefinitions(caerEventStreamOut outputStreams, size_t outputStreamsSize) {
	// If type is any, must be the only definition.
	if (outputStreamsSize == 1 && outputStreams[0].type == -1) {
		return;
	}

	for (size_t i = 0; i < outputStreamsSize; i++) {
		// Check type range.
		if (outputStreams[i].type < 0) {
			throw std::domain_error("Output stream has invalid type value.");
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && outputStreams[i - 1].type >= outputStreams[i].type) {
			throw std::domain_error("Output stream has invalid order of declaration or duplicates.");
		}
	}
}

/**
 * Check for the presence of the 'moduleInput' and 'moduleOutput' configuration
 * parameters, depending on the type of module and its requirements.
 */
static void checkModuleInputOutput(caerModuleInfo info, sshsNode configNode) {
	if (info->type == CAER_MODULE_INPUT) {
		// moduleInput must not exist for INPUT modules.
		if (sshsNodeAttributeExists(configNode, "moduleInput", SSHS_STRING)) {
			throw std::domain_error("INPUT type cannot have a 'moduleInput' attribute.");
		}
	}
	else {
		// CAER_MODULE_OUTPUT / CAER_MODULE_PROCESSOR
		// moduleInput must exist for OUTPUT and PROCESSOR modules.
		if (!sshsNodeAttributeExists(configNode, "moduleInput", SSHS_STRING)) {
			throw std::domain_error("OUTPUT/PROCESSOR types must have a 'moduleInput' attribute.");
		}
	}

	if (info->type == CAER_MODULE_OUTPUT) {
		// moduleOutput must not exist for OUTPUT modules.
		if (sshsNodeAttributeExists(configNode, "moduleOutput", SSHS_STRING)) {
			throw std::domain_error("OUTPUT type cannot have a 'moduleOutput' attribute.");
		}
	}
	else {
		// CAER_MODULE_INPUT / CAER_MODULE_PROCESSOR
		// moduleOutput must exist for INPUT and PROCESSOR modules, only
		// if their outputs are undefined (-1).
		if (info->outputStreams != NULL && info->outputStreamsSize == 1 && info->outputStreams[0].type == -1
			&& !sshsNodeAttributeExists(configNode, "moduleOutput", SSHS_STRING)) {
			throw std::domain_error(
				"INPUT/PROCESSOR types with ANY_TYPE definition must have a 'moduleOutput' attribute.");
		}
	}
}

static std::vector<int16_t> parseTypeIDString(const std::string &types) {
	// Empty string, cannot be!
	if (types.empty()) {
		throw std::invalid_argument("Empty Type ID string.");
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

	// Detect duplicates, which are not allowed.
	if (vectorDetectDuplicates(results)) {
		throw std::invalid_argument("Duplicate Type ID found.");
	}

	return (results);
}

static std::vector<OrderedInput> parseAugmentedTypeIDString(const std::string &types) {
	// Empty string, cannot be!
	if (types.empty()) {
		throw std::invalid_argument("Empty Augmented Type ID string.");
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
			const std::string orderString = typeString.substr(modifierPosition + 1);

			afterModuleOrder = std::stoi(orderString);

			// Check module ID value.
			if (afterModuleOrder < 0 || afterModuleOrder > INT16_MAX) {
				throw std::out_of_range("Module ID negative or too big.");
			}

			// Check that the module ID actually exists in the system.
			if (!caerMainloopModuleExists(static_cast<int16_t>(afterModuleOrder))) {
				throw std::out_of_range("Unknown module ID found.");
			}

			// Verify that the module ID belongs to a PROCESSOR module,
			// as only those can ever modify event streams and thus impose
			// an ordering on it and modules using it.
			if (!caerMainloopModuleIsType(static_cast<int16_t>(afterModuleOrder), CAER_MODULE_PROCESSOR)) {
				throw std::out_of_range("Module ID doesn't belong to a PROCESSOR type modules.");
			}
		}

		// Add extracted Type IDs to the result vector.
		results.push_back(OrderedInput(static_cast<int16_t>(type), static_cast<int16_t>(afterModuleOrder)));
	}

	// Ensure that something was extracted.
	if (results.empty()) {
		throw std::length_error("Empty extracted Augmented Type ID vector.");
	}

	// Detect duplicates, which are not allowed.
	// This because having the same type from the same source multiple times, even
	// if from different after-module points, would violate the event stream
	// uniqueness requirement for inputs and outputs, which is needed because it
	// would be impossible to distinguish packets inside a module if that were not
	// the case. For example we thus disallow 1[2a3,2a4] because inside the module
	// we would then have two packets with stream (1, 2), and no way to understand
	// which one was after being filtered by module ID 3 and which after module ID 4.
	// Augmenting the whole system to support such things is currently outside the
	// scope of this project, as it adds significant complexity with little or no
	// known gain, at least for the current use cases.
	if (vectorDetectDuplicates(results)) {
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
static void parseModuleInput(const std::string &inputDefinition,
	std::unordered_map<int16_t, std::vector<OrderedInput>> &resultMap, int16_t currId) {
	// Empty string, cannot be!
	if (inputDefinition.empty()) {
		throw std::invalid_argument("Empty 'moduleInput' attribute.");
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
			const std::string idString = matches[1];
			int id = std::stoi(idString);

			// Check module ID value.
			if (id < 0 || id > INT16_MAX) {
				throw std::out_of_range("Referenced module ID negative or too big.");
			}

			int16_t mId = static_cast<int16_t>(id);

			// If this module ID already exists in the map, it means there are
			// multiple definitions for the same ID; this is not allowed!
			if (resultMap.count(mId) == 1) {
				throw std::out_of_range("Duplicate referenced module ID found.");
			}

			// Check that the referenced module ID actually exists in the system.
			if (!caerMainloopModuleExists(mId)) {
				throw std::out_of_range("Unknown referenced module ID found.");
			}

			// Then get the various type IDs for that module.
			const std::string typeString = matches[2];

			resultMap[mId] = parseAugmentedTypeIDString(typeString);

			// Verify that the resulting event streams (sourceId, typeId) are
			// correct and do in fact exist.
			for (const auto &o : resultMap[mId]) {
				const auto foundEventStream = std::find(glMainloopData.streams.begin(), glMainloopData.streams.end(),
					ActiveStreams(mId, o.typeId));

				if (foundEventStream == glMainloopData.streams.end()) {
					// Specified event stream doesn't exist!
					throw std::out_of_range("Unknown event stream.");
				}
				else {
					// Event stream exists and is used here, mark it as used by
					// adding the current module ID to its users.
					foundEventStream->users.push_back(currId);
				}
			}

			iter++;
		}

		// inputDefinition was not empty, but we didn't manage to parse anything.
		if (resultMap.empty()) {
			throw std::length_error("Empty extracted input definition vector.");
		}
	}
	catch (const std::logic_error &ex) {
		// Clean map of any partial results on failure.
		resultMap.clear();

		throw std::logic_error(std::string("Invalid 'moduleInput' attribute: ") + typeid(ex).name() + ": " + ex.what());
	}
}

static void checkInputDefinitionAgainstEventStreamIn(
	std::unordered_map<int16_t, std::vector<OrderedInput>> &inputDefinition, caerEventStreamIn eventStreams,
	size_t eventStreamsSize) {
	// Use parsed moduleInput configuration to get per-type count.
	std::unordered_map<int, int> typeCount;

	for (const auto &in : inputDefinition) {
		for (const auto &typeAndOrder : in.second) {
			typeCount[typeAndOrder.typeId]++;
		}
	}

	// Any_Type/Any_Number means there just needs to be something.
	if (eventStreamsSize == 1 && eventStreams[0].type == -1 && eventStreams[0].number == -1) {
		if (typeCount.empty()) {
			throw std::domain_error("ANY_TYPE/ANY_NUMBER definition has no connected input streams.");
		}

		return; // We're good!
	}

	// Any_Type/1 means there must be exactly one type with count of 1.
	if (eventStreamsSize == 1 && eventStreams[0].type == -1 && eventStreams[0].number == 1) {
		if (typeCount.size() != 1 || typeCount.cbegin()->second != 1) {
			throw std::domain_error("ANY_TYPE/1 definition requires 1 connected input stream of some type.");
		}

		return; // We're good!
	}

	// All other cases involve possibly multiple definitions with a defined type.
	// Since EventStreamIn definitions are strictly monotonic in this case, we
	// first check that the number of definitions and counted types match.
	if (typeCount.size() != eventStreamsSize) {
		throw std::domain_error("DEFINED_TYPE definitions require as many connected types as given.");
	}

	for (size_t i = 0; i < eventStreamsSize; i++) {
		// Defined_Type/Any_Number means there must be 1 or more such types present.
		if (eventStreams[i].type >= 0 && eventStreams[i].number == -1) {
			if (typeCount[eventStreams[i].type] < 1) {
				throw std::domain_error(
					"DEFINED_TYPE/ANY_NUMBER definition requires at least one connected input stream of that type.");
			}
		}

		// Defined_Type/Defined_Number means there must be exactly as many such types present.
		if (eventStreams[i].type >= 0 && eventStreams[i].number > 0) {
			if (typeCount[eventStreams[i].type] != eventStreams[i].number) {
				throw std::domain_error(
					"DEFINED_TYPE/DEFINED_NUMBER definition requires exactly that many connected input streams of that type.");
			}
		}
	}
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
static void parseModuleOutput(const std::string &moduleOutput, std::vector<ModuleConnectivity> &outputs) {
	std::vector<int16_t> results = parseTypeIDString(moduleOutput);

	for (auto type : results) {
		outputs.push_back(ModuleConnectivity(type));
	}
}

static void parseEventStreamOutDefinition(caerEventStreamOut eventStreams, size_t eventStreamsSize,
	std::vector<ModuleConnectivity> &outputs) {
	for (size_t i = 0; i < eventStreamsSize; i++) {
		outputs.push_back(ModuleConnectivity(eventStreams[i].type));
	}
}

/**
 * An active event stream knows its origin (sourceId) and all of its users
 * (users vector). If the sourceId appears again inside the users vector
 * (possible for PROCESSORS that generate output data), there is a cycle.
 * Also if any of the users appear multiple times within the users vector,
 * there is a cycle. Cycles are not allowed and will result in an exception!
 */
static void checkForActiveStreamCycles(ActiveStreams &stream) {
	const auto foundSourceID = std::find(stream.users.begin(), stream.users.end(), stream.sourceId);

	if (foundSourceID != stream.users.end()) {
		// SourceId found inside users vector!
		throw std::domain_error(
			boost::str(
				boost::format("Found cycle back to Source ID in stream (%d, %d).") % stream.sourceId % stream.typeId));
	}

	// Detect duplicates, which are not allowed, as they signal a cycle.
	if (vectorDetectDuplicates(stream.users)) {
		throw std::domain_error(
			boost::str(boost::format("Found cycles in stream (%d, %d).") % stream.sourceId % stream.typeId));
	}
}

static std::vector<int16_t> getAllUsersForStreamAfterID(const ActiveStreams &stream, int16_t afterCheckId) {
	std::vector<int16_t> tmpOrder;

	for (auto id : stream.users) {
		for (const auto &order : glMainloopData.modules[id].inputDefinition[stream.sourceId]) {
			if (order.typeId == stream.typeId && order.afterModuleId == afterCheckId) {
				tmpOrder.push_back(id);
			}
		}
	}

	std::sort(tmpOrder.begin(), tmpOrder.end());

	return (tmpOrder);
}

static void orderActiveStreamDeps(const ActiveStreams &stream, std::shared_ptr<std::vector<DependencyNode>> &deps,
	int16_t checkId, size_t depth, std::vector<DependencyNode> *parent) {
	std::vector<int16_t> users = getAllUsersForStreamAfterID(stream, checkId);

	if (!users.empty()) {
		deps = std::make_shared<std::vector<DependencyNode>>();

		for (auto id : users) {
			DependencyNode d;
			d.id = id;
			d.depth = depth;
			d.parent = parent;
			orderActiveStreamDeps(stream, d.next, id, depth + 1, deps.get());
			deps->push_back(d);
		}
	}
}

static void printDeps(std::shared_ptr<std::vector<DependencyNode>> deps) {
	if (deps == nullptr) {
		return;
	}

	for (const auto &d : *deps) {
		for (size_t i = 0; i < d.depth; i++) {
			std::cout << "    ";
		}
		std::cout << d.id << std::endl;
		if (d.next) {
			printDeps(d.next);
		}
	}
}

static void mergeActiveStreamDeps(std::vector<ActiveStreams> &streams) {

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
		log(logLevel::ERROR, "Mainloop", "No modules configuration found.");
		return (EXIT_FAILURE);
	}

	for (size_t i = 0; i < modulesSize; i++) {
		sshsNode module = modules[i];
		const std::string moduleName = sshsNodeGetName(module);

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

		int16_t moduleId = sshsNodeGetShort(module, "moduleId");

		char *moduleLibraryC = sshsNodeGetString(module, "moduleLibrary");
		const std::string moduleLibrary = moduleLibraryC;
		free(moduleLibraryC);

		ModuleInfo info = ModuleInfo(moduleId, moduleName, module, moduleLibrary);

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

	// Free temporary configuration nodes array.
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

		for (const auto &p : modulePaths) {
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

		try {
			// Check that the modules respect the basic I/O definition requirements.
			checkInputOutputStreamDefinitions(info);

			// Check I/O event stream definitions for correctness.
			if (info->inputStreams != NULL) {
				checkInputStreamDefinitions(info->inputStreams, info->inputStreamsSize);
			}

			if (info->outputStreams != NULL) {
				checkOutputStreamDefinitions(info->outputStreams, info->outputStreamsSize);
			}

			checkModuleInputOutput(info, m.second.configNode);
		}
		catch (const std::logic_error &ex) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': %s", m.second.name.c_str(), ex.what());
			dlclose(moduleLibrary);
			continue;
		}

		m.second.libraryHandle = moduleLibrary;
		m.second.libraryInfo = info;
	}

	// If any modules failed to load, exit program now. We didn't do that before, so that we
	// could run through all modules and check them all in one go.
	for (const auto &m : glMainloopData.modules) {
		if (m.second.libraryHandle == NULL || m.second.libraryInfo == NULL) {
			// Clean up generated data on failure.
			glMainloopData.modules.clear();

			log(logLevel::ERROR, "Mainloop", "Errors in module library loading.");

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

	// Simple sanity check: at least 1 input and 1 output module must exist
	// to have a minimal, working system.
	if (inputModules.size() < 1 || outputModules.size() < 1) {
		// Clean up generated data on failure.
		glMainloopData.modules.clear();

		log(logLevel::ERROR, "Mainloop", "No input or output modules defined.");

		return (EXIT_FAILURE);
	}

	try {
		// Then we parse all the 'moduleOutput' configurations for certain INPUT
		// and PROCESSOR modules that have an ANY type declaration. If the types
		// are instead well defined, we parse the event stream definition directly.
		// We do this first so we can build up the map of all possible active event
		// streams, which we then can use for checking 'moduleInput' for correctness.
		for (const auto &m : boost::join(inputModules, processorModules)) {
			caerModuleInfo info = m.get().libraryInfo;

			if (info->outputStreams != NULL) {
				// ANY type declaration.
				if (info->outputStreamsSize == 1 && info->outputStreams[0].type == -1) {
					char *moduleOutput = sshsNodeGetString(m.get().configNode, "moduleOutput");
					const std::string outputDefinition = moduleOutput;
					free(moduleOutput);

					parseModuleOutput(outputDefinition, m.get().outputs);
				}
				else {
					parseEventStreamOutDefinition(info->outputStreams, info->outputStreamsSize, m.get().outputs);
				}

				// Now add discovered outputs to possible active streams.
				for (const auto &o : m.get().outputs) {
					ActiveStreams st = ActiveStreams(m.get().id, o.typeId);

					// Store if stream originates from a PROCESSOR (default from INPUT).
					if (info->type == CAER_MODULE_PROCESSOR) {
						st.isProcessor = true;
					}

					glMainloopData.streams.push_back(st);
				}
			}
		}

		// Then we parse all the 'moduleInput' configurations for OUTPUT and
		// PROCESSOR modules, which we can now verify against possible streams.
		for (const auto &m : boost::join(outputModules, processorModules)) {
			char *moduleInput = sshsNodeGetString(m.get().configNode, "moduleInput");
			const std::string inputDefinition = moduleInput;
			free(moduleInput);

			parseModuleInput(inputDefinition, m.get().inputDefinition, m.get().id);

			checkInputDefinitionAgainstEventStreamIn(m.get().inputDefinition, m.get().libraryInfo->inputStreams,
				m.get().libraryInfo->inputStreamsSize);
		}

		// At this point we can prune all event streams that are not marked active,
		// since this means nobody is referring to them.
		glMainloopData.streams.erase(
			std::remove_if(glMainloopData.streams.begin(), glMainloopData.streams.end(),
				[](const ActiveStreams &st) {return (st.users.empty());}), glMainloopData.streams.end());

		// If all event streams of an INPUT module are dropped, the module itself
		// is unconnected and useless, and that is a user configuration error.
		for (const auto &m : inputModules) {
			int16_t id = m.get().id;

			const auto iter = std::find_if(glMainloopData.streams.begin(), glMainloopData.streams.end(),
				[id](const ActiveStreams &st) {return (st.sourceId == id);});

			// No stream found for source ID corresponding to this module's ID.
			if (iter == glMainloopData.streams.end()) {
				boost::format exMsg = boost::format(
					"Module '%s': INPUT module is not connected to anything and will not be used.") % m.get().name;
				throw std::domain_error(exMsg.str());
			}
		}

		// At this point we know that all active event stream do come from some
		// active input module. We also know all of its follow-up users. Now those
		// user can specify data dependencies on that event stream, by telling after
		// which module they want to tap the stream for themselves. The only check
		// done on that specification up till now is that the module ID is valid and
		// exists, but it could refer to a module that's completely unrelated with
		// this event stream, and as such cannot be a valid point to tap into it.
		// We detect this now, as we have all the users of a stream listed in it.
		for (auto &st : glMainloopData.streams) {
			for (auto id : st.users) {
				for (auto &order : glMainloopData.modules[id].inputDefinition[st.sourceId]) {
					if (order.typeId == st.typeId && order.afterModuleId != -1) {
						// For each corresponding afterModuleId (that is not -1
						// which refers to original source ID and is always valid),
						// we check if we can find that ID inside of the stream's
						// users. If yes, then that's a valid tap point and we're
						// good; if no, this is a user configuration error.
						const auto iter = std::find_if(st.users.begin(), st.users.end(),
							[&order](int16_t moduleId) {return (order.afterModuleId == moduleId);});

						if (iter == st.users.end()) {
							boost::format exMsg = boost::format(
								"Module '%s': found invalid afterModuleID declaration of '%d' for stream (%d, %d).")
								% glMainloopData.modules[id].name % order.afterModuleId % st.sourceId % st.typeId;
							throw std::domain_error(exMsg.str());
						}
					}
				}
			}
		}

		// Detect cycles inside an active event stream.
		for (auto &st : glMainloopData.streams) {
			checkForActiveStreamCycles(st);
		}

		// Order event stream users according to the configuration.
		for (auto &st : glMainloopData.streams) {
			st.dependencies = std::make_shared<std::vector<DependencyNode>>();

			DependencyNode dRoot;
			dRoot.id = st.sourceId;
			dRoot.depth = 0;
			dRoot.parent = NULL;
			orderActiveStreamDeps(st, dRoot.next, -1, 1, st.dependencies.get());
			st.dependencies->push_back(dRoot);
		}

		// Now merge all streams and their users into one global order over
		// all modules. If this cannot be resolved, wrong connections or a
		// cycle involving multiple streams are present.
		mergeActiveStreamDeps(glMainloopData.streams);

		// There's multiple ways now to build the full connectivity graph once we
		// have all the starting points. Since we do have a global execution order
		// (see above), we can just visit the modules in that order and build
		// all the inputs and outputs.
		// TODO: detect processors that serve no purpose, ie. no output or unused
		// output, as well as no further users of modified inputs.
		for (const auto &m : inputModules) {
			int16_t currInputModuleId = m.get().id;

		}
	}
	catch (const std::exception &ex) {
		// Cleanup modules and streams on exit.
		glMainloopData.modules.clear();
		glMainloopData.streams.clear();

		log(logLevel::ERROR, "Mainloop", ex.what());

		return (EXIT_FAILURE);
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

	for (const auto &st : glMainloopData.streams) {
		std::cout << "(" << st.sourceId << ", " << st.typeId << ") - IS_PROC: " << st.isProcessor << " - ";
		for (auto mid : st.users) {
			std::cout << mid << ", ";
		}
		std::cout << std::endl;
		printDeps(st.dependencies);
	}

	for (const auto &m : glMainloopData.modules) {
		std::cout << m.second.id << "-MOD:" << m.second.libraryInfo->type << "-" << m.second.name << std::endl;

		for (const auto &i : m.second.inputs) {
			std::cout << " -->" << i.typeId << "-IN" << std::endl;
		}

		for (const auto &o : m.second.outputs) {
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

	log(logLevel::INFO, "Mainloop", "Started successfully.");

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

	// Cleanup modules and streams on exit.
	glMainloopData.modules.clear();
	glMainloopData.streams.clear();

	log(logLevel::INFO, "Mainloop", "Terminated successfully.");

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

bool caerMainloopModuleExists(int16_t id) {
	return (glMainloopData.modules.count(id) == 1);
}

bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type) {
	return (glMainloopData.modules.at(id).libraryInfo->type == type);
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
