#ifndef SSHS_HPP_
#define SSHS_HPP_

#include "sshs.h"

#include <string>

inline void sshsNodeCreate(sshsNode node, const char *key, bool defaultValue, enum sshs_node_attr_flags flags,
	const char *description) {
	sshsNodeCreateBool(node, key, defaultValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateByte(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue,
	int16_t maxValue, enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateShort(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue,
	int32_t maxValue, enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue,
	int64_t maxValue, enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateLong(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue,
	enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateFloat(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateDouble(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, const char *defaultValue, size_t minLength,
	size_t maxLength, enum sshs_node_attr_flags flags, const char *description) {
	sshsNodeCreateString(node, key, defaultValue, minLength, maxLength, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, const std::string &defaultValue, size_t minLength,
	size_t maxLength, enum sshs_node_attr_flags flags, const std::string &description) {
	sshsNodeCreateString(node, key, defaultValue.c_str(), minLength, maxLength, flags, description.c_str());
}

inline void sshsNodePut(sshsNode node, const char *key, bool value) {
	sshsNodePutBool(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, int8_t value) {
	sshsNodePutByte(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, int16_t value) {
	sshsNodePutShort(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, int32_t value) {
	sshsNodePutInt(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, int64_t value) {
	sshsNodePutLong(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, float value) {
	sshsNodePutFloat(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, double value) {
	sshsNodePutDouble(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, const char *value) {
	sshsNodePutString(node, key, value);
}

inline void sshsNodePut(sshsNode node, const char *key, const std::string &value) {
	sshsNodePutString(node, key, value.c_str());
}

// Additional getter for std::string.
inline std::string sshsNodeGetStdString(sshsNode node, const char *key) {
	char *str = sshsNodeGetString(node, key);
	std::string cppStr(str);
	free(str);
	return (cppStr);
}

#endif /* SSHS_HPP_ */
