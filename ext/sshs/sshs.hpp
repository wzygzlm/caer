#ifndef SSHS_HPP_
#define SSHS_HPP_

#include "sshs.h"

#include <string>
#include <cstring>
#include <stdexcept>

class sshs_value {
private:
	enum sshs_node_attr_value_type type;
	union {
		bool boolean;
		int8_t ibyte;
		int16_t ishort;
		int32_t iint;
		int64_t ilong;
		float ffloat;
		double ddouble;
	} value;
	std::string valueString; // Separate for easy memory management.

public:
	sshs_value() {
		type = SSHS_UNKNOWN;
		value.ilong = 0;
	}

	enum sshs_node_attr_value_type getType() const noexcept {
		return (type);
	}

	bool getBool() const {
		if (type != SSHS_BOOL) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.boolean);
	}

	void setBool(bool v) noexcept {
		type = SSHS_BOOL;
		value.boolean = v;
	}

	int8_t getByte() const {
		if (type != SSHS_BYTE) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ibyte);
	}

	void setByte(int8_t v) noexcept {
		type = SSHS_BYTE;
		value.ibyte = v;
	}

	int16_t getShort() const {
		if (type != SSHS_SHORT) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ishort);
	}

	void setShort(int16_t v) noexcept {
		type = SSHS_SHORT;
		value.ishort = v;
	}

	int32_t getInt() const {
		if (type != SSHS_INT) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.iint);
	}

	void setInt(int32_t v) noexcept {
		type = SSHS_INT;
		value.iint = v;
	}

	int64_t getLong() const {
		if (type != SSHS_LONG) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ilong);
	}

	void setLong(int64_t v) noexcept {
		type = SSHS_LONG;
		value.ilong = v;
	}

	float getFloat() const {
		if (type != SSHS_FLOAT) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ffloat);
	}

	void setFloat(float v) noexcept {
		type = SSHS_FLOAT;
		value.ffloat = v;
	}

	double getDouble() const {
		if (type != SSHS_DOUBLE) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (value.ddouble);
	}

	void setDouble(double v) noexcept {
		type = SSHS_DOUBLE;
		value.ddouble = v;
	}

	const std::string &getString() const {
		if (type != SSHS_STRING) {
			throw new std::runtime_error("SSHS: value type does not match requested type.");
		}

		return (valueString);
	}

	void setString(const std::string &v) noexcept {
		type = SSHS_STRING;
		valueString = v;
	}

	void fromCUnion(union sshs_node_attr_value vu, enum sshs_node_attr_value_type tu) {
		switch (tu) {
			case SSHS_BOOL:
				setBool(vu.boolean);
				break;

			case SSHS_BYTE:
				setByte(vu.ibyte);
				break;

			case SSHS_SHORT:
				setShort(vu.ishort);
				break;

			case SSHS_INT:
				setInt(vu.iint);
				break;

			case SSHS_LONG:
				setLong(vu.ilong);
				break;

			case SSHS_FLOAT:
				setFloat(vu.ffloat);
				break;

			case SSHS_DOUBLE:
				setDouble(vu.ddouble);
				break;

			case SSHS_STRING:
				setString(vu.string);
				break;

			case SSHS_UNKNOWN:
			default:
				throw new std::runtime_error("SSHS: provided union value type does not match any valid type.");
				break;
		}
	}

	union sshs_node_attr_value toCUnion(bool readOnlyString = false) const {
		union sshs_node_attr_value vu;

		switch (type) {
			case SSHS_BOOL:
				vu.boolean = getBool();
				break;

			case SSHS_BYTE:
				vu.ibyte = getByte();
				break;

			case SSHS_SHORT:
				vu.ishort = getShort();
				break;

			case SSHS_INT:
				vu.iint = getInt();
				break;

			case SSHS_LONG:
				vu.ilong = getLong();
				break;

			case SSHS_FLOAT:
				vu.ffloat = getFloat();
				break;

			case SSHS_DOUBLE:
				vu.ddouble = getDouble();
				break;

			case SSHS_STRING:
				if (readOnlyString) {
					vu.string = const_cast<char *>(getString().c_str());
				}
				else {
					vu.string = strdup(getString().c_str());
				}
				break;

			case SSHS_UNKNOWN:
			default:
				throw new std::runtime_error("SSHS: internal value type does not match any valid type.");
				break;
		}

		return (vu);
	}

	// Comparison operators.
	bool operator==(const sshs_value &rhs) const {
		switch (type) {
			case SSHS_BOOL:
				return (getBool() == rhs.getBool());

			case SSHS_BYTE:
				return (getByte() == rhs.getByte());

			case SSHS_SHORT:
				return (getShort() == rhs.getShort());

			case SSHS_INT:
				return (getInt() == rhs.getInt());

			case SSHS_LONG:
				return (getLong() == rhs.getLong());

			case SSHS_FLOAT:
				return (getFloat() == rhs.getFloat());

			case SSHS_DOUBLE:
				return (getDouble() == rhs.getDouble());

			case SSHS_STRING:
				return (getString() == rhs.getString());

			case SSHS_UNKNOWN:
			default:
				return (false);
		}
	}

	bool operator!=(const sshs_value &rhs) const {
		return (!this->operator==(rhs));
	}

	bool valueInRange(union sshs_node_attr_range min, union sshs_node_attr_range max) const {
		switch (type) {
			case SSHS_BOOL:
				// No check for bool, because no range exists.
				return (true);

			case SSHS_BYTE:
				return (value.ibyte >= min.ibyteRange && value.ibyte <= max.ibyteRange);

			case SSHS_SHORT:
				return (value.ishort >= min.ishortRange && value.ishort <= max.ishortRange);

			case SSHS_INT:
				return (value.iint >= min.iintRange && value.iint <= max.iintRange);

			case SSHS_LONG:
				return (value.ilong >= min.ilongRange && value.ilong <= max.ilongRange);

			case SSHS_FLOAT:
				return (value.ffloat >= min.ffloatRange && value.ffloat <= max.ffloatRange);

			case SSHS_DOUBLE:
				return (value.ddouble >= min.ddoubleRange && value.ddouble <= max.ddoubleRange);

			case SSHS_STRING:
				return (valueString.length() >= min.stringRange && valueString.length() <= max.stringRange);

			case SSHS_UNKNOWN:
			default:
				return (false);
		}
	}
};

// Helper functions
std::string sshsHelperTypeToStringConverter(enum sshs_node_attr_value_type type) CAER_SYMBOL_EXPORT;
enum sshs_node_attr_value_type sshsHelperStringToTypeConverter(const std::string &typeString) CAER_SYMBOL_EXPORT;
std::string sshsHelperValueToStringConverter(const sshs_value &val) CAER_SYMBOL_EXPORT;
sshs_value sshsHelperStringToValueConverter(enum sshs_node_attr_value_type type, const std::string &valueString) CAER_SYMBOL_EXPORT;

sshs_value sshsNodeGetAttribute(sshsNode node, const std::string &key, enum sshs_node_attr_value_type type =
	SSHS_UNKNOWN);

inline void sshsNodeCreate(sshsNode node, const char *key, bool defaultValue, int flags, const char *description) {
	sshsNodeCreateBool(node, key, defaultValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateByte(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateShort(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateInt(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	int flags, const char *description) {
	sshsNodeCreateLong(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue,
	int flags, const char *description) {
	sshsNodeCreateFloat(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	int flags, const char *description) {
	sshsNodeCreateDouble(node, key, defaultValue, minValue, maxValue, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	int flags, const char *description) {
	sshsNodeCreateString(node, key, defaultValue, minLength, maxLength, flags, description);
}

inline void sshsNodeCreate(sshsNode node, const char *key, const std::string &defaultValue, size_t minLength,
	size_t maxLength, int flags, const std::string &description) {
	sshsNodeCreateString(node, key, defaultValue.c_str(), minLength, maxLength, flags, description.c_str());
}

inline bool sshsNodePut(sshsNode node, const char *key, bool value) {
	return (sshsNodePutBool(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, int8_t value) {
	return (sshsNodePutByte(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, int16_t value) {
	return (sshsNodePutShort(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, int32_t value) {
	return (sshsNodePutInt(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, int64_t value) {
	return (sshsNodePutLong(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, float value) {
	return (sshsNodePutFloat(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, double value) {
	return (sshsNodePutDouble(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, const char *value) {
	return (sshsNodePutString(node, key, value));
}

inline bool sshsNodePut(sshsNode node, const char *key, const std::string &value) {
	return (sshsNodePutString(node, key, value.c_str()));
}

// Additional getter for std::string.
inline std::string sshsNodeGetStdString(sshsNode node, const char *key) {
	char *str = sshsNodeGetString(node, key);
	std::string cppStr(str);
	free(str);
	return (cppStr);
}

// Additional updater for std::string.
inline bool sshsNodeUpdateReadOnlyAttribute(sshsNode node, const char *key, const std::string &value) {
	union sshs_node_attr_value newValue;
	newValue.string = const_cast<char *>(value.c_str());
	return (sshsNodeUpdateReadOnlyAttribute(node, key, SSHS_STRING, newValue));
}

// std::string variants of node getters.
inline bool sshsExistsNode(sshs st, const std::string &nodePath) {
	return (sshsExistsNode(st, nodePath.c_str()));
}

inline sshsNode sshsGetNode(sshs st, const std::string &nodePath) {
	return (sshsGetNode(st, nodePath.c_str()));
}

inline bool sshsExistsRelativeNode(sshsNode node, const std::string &nodePath) {
	return (sshsExistsRelativeNode(node, nodePath.c_str()));
}

inline sshsNode sshsGetRelativeNode(sshsNode node, const std::string &nodePath) {
	return (sshsGetRelativeNode(node, nodePath.c_str()));
}

#endif /* SSHS_HPP_ */
