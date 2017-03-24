#ifndef SSHS_H_
#define SSHS_H_

// Common includes, useful for everyone.
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

// SSHS node
typedef struct sshs_node *sshsNode;

enum sshs_node_attr_value_type {
	SSHS_UNKNOWN = -1,
	SSHS_BOOL = 0,
	SSHS_BYTE = 1,
	SSHS_SHORT = 2,
	SSHS_INT = 3,
	SSHS_LONG = 4,
	SSHS_FLOAT = 5,
	SSHS_DOUBLE = 6,
	SSHS_STRING = 7,
};

union sshs_node_attr_value {
	bool boolean;
	int8_t ibyte;
	int16_t ishort;
	int32_t iint;
	int64_t ilong;
	float ffloat;
	double ddouble;
	char *string;
};

union sshs_node_attr_range {
	double d;
	int64_t i;
};

enum sshs_node_attr_flags {
	SSHS_ATTRIBUTE_NORMAL = 0,
	SSHS_ATTRIBUTE_READ_ONLY = 1,
	SSHS_ATTRIBUTE_NOTIFY_ONLY = 2,
};

enum sshs_node_node_events {
	SSHS_CHILD_NODE_ADDED = 0,
};

enum sshs_node_attribute_events {
	SSHS_ATTRIBUTE_ADDED = 0,
	SSHS_ATTRIBUTE_MODIFIED = 1,
};

const char *sshsNodeGetName(sshsNode node);
const char *sshsNodeGetPath(sshsNode node);
sshsNode sshsNodeGetParent(sshsNode node);
sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren); // Walk all children.
void sshsNodeAddNodeListener(sshsNode node, void *userData,
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode));
void sshsNodeRemoveNodeListener(sshsNode node, void *userData,
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode));
void sshsNodeRemoveAllNodeListeners(sshsNode node);
void sshsNodeAddAttributeListener(sshsNode node, void *userData,
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue));
void sshsNodeRemoveAttributeListener(sshsNode node, void *userData,
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue));
void sshsNodeRemoveAllAttributeListeners(sshsNode node);
void sshsNodeCreateAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value defaultValue, union sshs_node_attr_range minValue, union sshs_node_attr_range maxValue,
	enum sshs_node_attr_flags flags);
bool sshsNodeAttributeExists(sshsNode node, const char *key, enum sshs_node_attr_value_type type);
bool sshsNodePutAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value value);
union sshs_node_attr_value sshsNodeGetAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type);
void sshsNodeCreateBool(sshsNode node, const char *key, bool defaultValue, enum sshs_node_attr_flags flags);
void sshsNodePutBool(sshsNode node, const char *key, bool value);
bool sshsNodeGetBool(sshsNode node, const char *key);
void sshsNodeCreateByte(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	enum sshs_node_attr_flags flags);
void sshsNodePutByte(sshsNode node, const char *key, int8_t value);
int8_t sshsNodeGetByte(sshsNode node, const char *key);
void sshsNodeCreateShort(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	enum sshs_node_attr_flags flags);
void sshsNodePutShort(sshsNode node, const char *key, int16_t value);
int16_t sshsNodeGetShort(sshsNode node, const char *key);
void sshsNodeCreateInt(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	enum sshs_node_attr_flags flags);
void sshsNodePutInt(sshsNode node, const char *key, int32_t value);
int32_t sshsNodeGetInt(sshsNode node, const char *key);
void sshsNodeCreateLong(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	enum sshs_node_attr_flags flags);
void sshsNodePutLong(sshsNode node, const char *key, int64_t value);
int64_t sshsNodeGetLong(sshsNode node, const char *key);
void sshsNodeCreateFloat(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue,
	enum sshs_node_attr_flags flags);
void sshsNodePutFloat(sshsNode node, const char *key, float value);
float sshsNodeGetFloat(sshsNode node, const char *key);
void sshsNodeCreateDouble(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	enum sshs_node_attr_flags flags);
void sshsNodePutDouble(sshsNode node, const char *key, double value);
double sshsNodeGetDouble(sshsNode node, const char *key);
void sshsNodeCreateString(sshsNode node, const char *key, const char *defaultValue, size_t minLength,
	size_t maxLength, enum sshs_node_attr_flags flags);
void sshsNodePutString(sshsNode node, const char *key, const char *value);
char *sshsNodeGetString(sshsNode node, const char *key);
void sshsNodeExportNodeToXML(sshsNode node, int outFd, const char **filterKeys, size_t filterKeysLength);
void sshsNodeExportSubTreeToXML(sshsNode node, int outFd, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength);
bool sshsNodeImportNodeFromXML(sshsNode node, int inFd, bool strict);
bool sshsNodeImportSubTreeFromXML(sshsNode node, int inFd, bool strict);
bool sshsNodeStringToNodeConverter(sshsNode node, const char *key, const char *type, const char *value);
const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames);
const char **sshsNodeGetAttributeKeys(sshsNode node, size_t *numKeys);
enum sshs_node_attr_value_type *sshsNodeGetAttributeTypes(sshsNode node, const char *key, size_t *numTypes);
union sshs_node_attr_range sshsNodeGetAttributeMinRange(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type);
union sshs_node_attr_range sshsNodeGetAttributeMaxRange(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type);
enum sshs_node_attr_flags sshsNodeGetAttributeFlags(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type);

// Helper functions
const char *sshsHelperTypeToStringConverter(enum sshs_node_attr_value_type type);
enum sshs_node_attr_value_type sshsHelperStringToTypeConverter(const char *typeString);
char *sshsHelperValueToStringConverter(enum sshs_node_attr_value_type type, union sshs_node_attr_value value);
bool sshsHelperStringToValueConverter(enum sshs_node_attr_value_type type, const char *valueString,
	union sshs_node_attr_value *value);

// SSHS
typedef struct sshs_struct *sshs;
typedef void (*sshsErrorLogCallback)(const char *msg);

sshs sshsGetGlobal(void);
void sshsSetGlobalErrorLogCallback(sshsErrorLogCallback error_log_cb);
sshs sshsNew(void);
bool sshsExistsNode(sshs st, const char *nodePath);
sshsNode sshsGetNode(sshs st, const char *nodePath);
bool sshsExistsRelativeNode(sshsNode node, const char *nodePath);
sshsNode sshsGetRelativeNode(sshsNode node, const char *nodePath);
bool sshsBeginTransaction(sshs st, char *nodePaths[], size_t nodePathsLength);
bool sshsEndTransaction(sshs st, char *nodePaths[], size_t nodePathsLength);

#endif /* SSHS_H_ */
