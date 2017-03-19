#ifndef BASE_MODULEINTERFACE_HPP_
#define BASE_MODULEINTERFACE_HPP_

namespace caer {

// Module-related definitions.
enum class ModuleStatus {
	STOPPED = 0, RUNNING = 1,
};

enum class ModuleType {
	INPUT = 0, OUTPUT = 1, PROCESSOR = 2,
};

struct EventStreamStruct {
	int16_t type;
	int16_t number; // Use 0 for any number of.
	bool required;
	// Support chaining for multiple elements.
	EventStream next;
};

typedef struct EventStreamStruct *EventStream;

struct PluginInfo {
	uint32_t version;
	char *name;
	ModuleType type;
	EventStream inputStreams;
	EventStream outputStreams;
};

struct ModuleFunctions {
	bool (* const moduleInit)(caerModuleData moduleData); // Can be NULL.
	void (* const moduleRun)(caerModuleData moduleData, size_t argsNumber, va_list args);
	void (* const moduleConfig)(caerModuleData moduleData); // Can be NULL.
	void (* const moduleExit)(caerModuleData moduleData); // Can be NULL.
	void (* const moduleReset)(caerModuleData moduleData, uint16_t resetCallSourceID); // Can be NULL.
};

struct ConfigParameter {
	const char *key;
	enum sshs_node_attr_value_type type;
	union sshs_node_attr_value value;
	union sshs_node_attr_range min;
	union sshs_node_attr_range max;
	enum sshs_node_attr_flags flags;
};

struct ModuleConfig {

};

class ModuleInterface {
private:
	int16_t id;
	sshsNode moduleNode;
	ModuleStatus moduleStatus;
	atomic_bool running;
	atomic_uint_fast32_t configUpdate;
	void *moduleState;
	char *moduleSubSystemString;

	void *parentMainLoop;

public:
	ModuleInterface();
	virtual ~ModuleInterface();
};

} /* namespace caer */

#endif /* BASE_MODULEINTERFACE_HPP_ */
