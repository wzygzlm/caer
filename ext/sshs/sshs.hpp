#ifndef SSHS_HPP_
#define SSHS_HPP_

#include "sshs.h"

#include <string>
#include <limits>

static inline void sshsNodeCreate(sshsNode node, const char *key, bool defaultValue, enum sshs_node_attr_flags flags =
	SSHS_FLAGS_NORMAL) {
	sshsNodeCreateBool(node, key, defaultValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue =
	std::numeric_limits<int8_t>::min(), int8_t maxValue = std::numeric_limits<int8_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateByte(node, key, defaultValue, minValue, maxValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue =
	std::numeric_limits<int16_t>::min(), int16_t maxValue = std::numeric_limits<int16_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateShort(node, key, defaultValue, minValue, maxValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue =
	std::numeric_limits<int32_t>::min(), int32_t maxValue = std::numeric_limits<int32_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue =
	std::numeric_limits<int64_t>::min(), int64_t maxValue = std::numeric_limits<int64_t>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateLong(node, key, defaultValue, minValue, maxValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, float defaultValue, float minValue =
	-std::numeric_limits<float>::max(), float maxValue = std::numeric_limits<float>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateFloat(node, key, defaultValue, minValue, maxValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, double defaultValue, double minValue =
	-std::numeric_limits<double>::max(), double maxValue = std::numeric_limits<double>::max(),
	enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateDouble(node, key, defaultValue, minValue, maxValue, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, const char *defaultValue, size_t minLength = 0,
	size_t maxLength = SIZE_MAX, enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateString(node, key, defaultValue, minLength, maxLength, flags);
}

static inline void sshsNodeCreate(sshsNode node, const char *key, const std::string &defaultValue, size_t minLength = 0,
	size_t maxLength = SIZE_MAX, enum sshs_node_attr_flags flags = SSHS_FLAGS_NORMAL) {
	sshsNodeCreateString(node, key, defaultValue.c_str(), minLength, maxLength, flags);
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

template<class T>
static inline T sshsNodeGet(sshsNode node, const char *key) {
	static_assert(true, "sshsNodeGet() can only be used with bool, int{8,16,32,64}_t, float, double and strings.");
}

template<>
static inline bool sshsNodeGet<bool>(sshsNode node, const char *key) {
	return (sshsNodeGetBool(node, key));
}

template<>
static inline int8_t sshsNodeGet<int8_t>(sshsNode node, const char *key) {
	return (sshsNodeGetByte(node, key));
}

template<>
static inline int16_t sshsNodeGet<int16_t>(sshsNode node, const char *key) {
	return (sshsNodeGetShort(node, key));
}

template<>
static inline int32_t sshsNodeGet<int32_t>(sshsNode node, const char *key) {
	return (sshsNodeGetInt(node, key));
}

template<>
static inline int64_t sshsNodeGet<int64_t>(sshsNode node, const char *key) {
	return (sshsNodeGetLong(node, key));
}

template<>
static inline float sshsNodeGet<float>(sshsNode node, const char *key) {
	return (sshsNodeGetFloat(node, key));
}

template<>
static inline double sshsNodeGet<double>(sshsNode node, const char *key) {
	return (sshsNodeGetDouble(node, key));
}

template<>
static inline char *sshsNodeGet<char *>(sshsNode node, const char *key) {
	return (sshsNodeGetString(node, key));
}

template<>
static inline const char *sshsNodeGet<const char *>(sshsNode node, const char *key) {
	return (sshsNodeGetString(node, key));
}

template<>
static inline std::string sshsNodeGet<std::string>(sshsNode node, const char *key) {
	char *str = sshsNodeGetString(node, key);
	std::string cppStr(str);
	free(str);
	return (cppStr);
}

template<>
static inline const std::string sshsNodeGet<const std::string>(sshsNode node, const char *key) {
	char *str = sshsNodeGetString(node, key);
	const std::string cppStr(str);
	free(str);
	return (cppStr);
}

#endif /* SSHS_HPP_ */
