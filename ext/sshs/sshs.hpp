#ifndef SSHS_HPP_
#define SSHS_HPP_

#include "sshs.h"

#include <string>
#include <limits>

static inline void sshsNodeCreate(sshsNode node, const char *key, bool defaultValue, const char *description,
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateBool(node, key, defaultValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int8_t defaultValue, const char *description,
	int8_t minValue = std::numeric_limits<int8_t>::min(), int8_t maxValue = std::numeric_limits<int8_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateByte(node, key, defaultValue, minValue, maxValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int16_t defaultValue, const char *description,
	int16_t minValue = std::numeric_limits<int16_t>::min(), int16_t maxValue = std::numeric_limits<int16_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateShort(node, key, defaultValue, minValue, maxValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int32_t defaultValue, const char *description,
	int32_t minValue = std::numeric_limits<int32_t>::min(), int32_t maxValue = std::numeric_limits<int32_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int64_t defaultValue, const char *description,
	int64_t minValue = std::numeric_limits<int64_t>::min(), int64_t maxValue = std::numeric_limits<int64_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateLong(node, key, defaultValue, minValue, maxValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, float defaultValue, const char *description,
	float minValue = -std::numeric_limits<float>::max(), float maxValue = std::numeric_limits<float>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateFloat(node, key, defaultValue, minValue, maxValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, double defaultValue, const char *description,
	double minValue = -std::numeric_limits<double>::max(), double maxValue = std::numeric_limits<double>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateDouble(node, key, defaultValue, minValue, maxValue, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, const char *defaultValue, const char *description,
	size_t minLength = 0, size_t maxLength = SIZE_MAX, enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateString(node, key, defaultValue, minLength, maxLength, flags, description);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, const std::string &defaultValue,
	const std::string &description, size_t minLength = 0, size_t maxLength = SIZE_MAX, enum sshs_node_attr_flags flags =
		SSHS_FLAGS_NORMAL) {
	sshsNodeCreateString(node, key, defaultValue.c_str(), minLength, maxLength, flags, description.c_str());
}

static inline void sshsNodePut(sshsNode node, const char *key, bool value) {
	sshsNodePutBool(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, int8_t value) {
	sshsNodePutByte(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, int16_t value) {
	sshsNodePutShort(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, int32_t value) {
	sshsNodePutInt(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, int64_t value) {
	sshsNodePutLong(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, float value) {
	sshsNodePutFloat(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, double value) {
	sshsNodePutDouble(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, const char *value) {
	sshsNodePutString(node, key, value);
}

static inline void sshsNodePut(sshsNode node, const char *key, const std::string &value) {
	sshsNodePutString(node, key, value.c_str());
}

// Additional getter for std::string.
static inline std::string sshsNodeGetStdString(sshsNode node, const char *key) {
	char *str = sshsNodeGetString(node, key);
	std::string cppStr(str);
	free(str);
	return (cppStr);
}

#endif /* SSHS_HPP_ */
