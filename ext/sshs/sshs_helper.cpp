#include "sshs_internal.h"

std::string sshsHelperCppTypeToStringConverter(enum sshs_node_attr_value_type type)  {
	// Convert the value and its type into a string for XML output.
	switch (type) {
		case SSHS_BOOL:
			return ("bool");

		case SSHS_BYTE:
			return ("byte");

		case SSHS_SHORT:
			return ("short");

		case SSHS_INT:
			return ("int");

		case SSHS_LONG:
			return ("long");

		case SSHS_FLOAT:
			return ("float");

		case SSHS_DOUBLE:
			return ("double");

		case SSHS_STRING:
			return ("string");

		case SSHS_UNKNOWN:
		default:
			throw new std::runtime_error("sshsHelperTypeToStringConverter(): invalid type given.");
	}
}

enum sshs_node_attr_value_type sshsHelperCppStringToTypeConverter(const std::string &typeString) {
	// Convert the value string back into the internal type representation.
	if (typeString == "bool") {
		return (SSHS_BOOL);
	}
	else if (typeString == "byte") {
		return (SSHS_BYTE);
	}
	else if (typeString == "short") {
		return (SSHS_SHORT);
	}
	else if (typeString == "int") {
		return (SSHS_INT);
	}
	else if (typeString == "long") {
		return (SSHS_LONG);
	}
	else if (typeString == "float") {
		return (SSHS_FLOAT);
	}
	else if (typeString == "double") {
		return (SSHS_DOUBLE);
	}
	else if (typeString == "string") {
		return (SSHS_STRING);
	}

	return (SSHS_UNKNOWN); // UNKNOWN TYPE.
}

std::string sshsHelperCppValueToStringConverter(const sshs_value &val) {
	// Convert the value and its type into a string for XML output.
	switch (val.getType()) {
		case SSHS_BOOL:
			// Manually generate true or false.
			return ((val.getBool()) ? ("true") : ("false"));

		case SSHS_BYTE:
			return (std::to_string(val.getByte()));

		case SSHS_SHORT:
			return (std::to_string(val.getShort()));

		case SSHS_INT:
			return (std::to_string(val.getInt()));

		case SSHS_LONG:
			return (std::to_string(val.getLong()));

		case SSHS_FLOAT:
			return (std::to_string(val.getFloat()));

		case SSHS_DOUBLE:
			return (std::to_string(val.getDouble()));

		case SSHS_STRING:
			return (val.getString());

		case SSHS_UNKNOWN:
		default:
			throw new std::runtime_error("sshsHelperValueToStringConverter(): value has invalid type.");
	}
}

// Return false on failure (unknown type / faulty conversion), the content of
// value is undefined. For the STRING type, the returned value.string is a copy
// of the input string. Remember to free() it after use!
sshs_value sshsHelperCppStringToValueConverter(enum sshs_node_attr_value_type type, const std::string &valueString) {
	sshs_value value;

	switch (type) {
		case SSHS_BOOL:
			// Boolean uses custom true/false strings.
			value.setBool(valueString == "true");
			break;

		case SSHS_BYTE:
			value.setByte((int8_t) std::stoi(valueString));
			break;

		case SSHS_SHORT:
			value.setShort((int16_t) std::stoi(valueString));
			break;

		case SSHS_INT:
			value.setInt((int32_t) std::stol(valueString));
			break;

		case SSHS_LONG:
			value.setLong((int64_t) std::stoll(valueString));
			break;

		case SSHS_FLOAT:
			value.setFloat(std::stof(valueString));
			break;

		case SSHS_DOUBLE:
			value.setDouble(std::stod(valueString));
			break;

		case SSHS_STRING:
			value.setString(valueString);
			break;

		case SSHS_UNKNOWN:
		default:
			break;
	}

	return (value);
}
