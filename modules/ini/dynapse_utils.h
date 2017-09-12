#ifndef MODULES_INI_DYNAPSE_UTILS_H_
#define MODULES_INI_DYNAPSE_UTILS_H_

#include "main.h"
#include <libcaer/devices/dynapse.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const char *chipIDToName(int16_t chipID, bool withEndSlash) {
	switch (chipID) {
		case DYNAPSE_CONFIG_DYNAPSE_U0: {
			return ((withEndSlash) ? ("DYNAPSE_CONFIG_DYNAPSE_U0/") : ("DYNAPSE_CONFIG_DYNAPSE_U0"));
			break;
		}
		case DYNAPSE_CONFIG_DYNAPSE_U1: {
			return ((withEndSlash) ? ("DYNAPSE_CONFIG_DYNAPSE_U1/") : ("DYNAPSE_CONFIG_DYNAPSE_U1"));
			break;
		}
		case DYNAPSE_CONFIG_DYNAPSE_U2: {
			return ((withEndSlash) ? ("DYNAPSE_CONFIG_DYNAPSE_U2/") : ("DYNAPSE_CONFIG_DYNAPSE_U2"));
			break;
		}
		case DYNAPSE_CONFIG_DYNAPSE_U3: {
			return ((withEndSlash) ? ("DYNAPSE_CONFIG_DYNAPSE_U3/") : ("DYNAPSE_CONFIG_DYNAPSE_U3"));
			break;
		}
		case DYNAPSE_CHIP_DYNAPSE: {
			return ((withEndSlash) ? ("DYNAPSEFX2/") : ("DYNAPSEFX2"));
			break;
		}
	}

	return ((withEndSlash) ? ("Unknown/") : ("Unknown"));
}

/**
 * Set a certain bias of a specific core of a chip of the Dynap-SE device
 * to a user-supplied value.
 *
 * @param dynapseNode Dynap-SE module configuration node (source node).
 * @param chipId Chip ID.
 * @param coreId Core ID.
 * @param biasName Bias name, like "IF_RFR_N" or "IF_DC_P".
 * @param coarseValue coarse current value, range [0,7], 0 is highest current, 7 lowest.
 * @param fineValue fine current value, range [0,255], 0 is lowest current, 255 highest.
 * @param highLow bias current level, choices are 'High' (true) and 'Low' (false).
 */
static inline void caerDynapseSetBiasCore(sshsNode dynapseNode, uint8_t chipId, uint8_t coreId, const char *biasName,
	uint8_t coarseValue, uint8_t fineValue, bool highLow) {
	// Check if the pointer is valid.
	if (dynapseNode == NULL) {
		return;
	}

	if (chipId >= 4) {
		caerLog(CAER_LOG_ERROR, __func__, "Chip ID %d is invalid.", chipId);
		return;
	}

	if (coreId >= 4) {
		caerLog(CAER_LOG_ERROR, __func__, "Core ID %d is invalid.", coreId);
		return;
	}

	size_t biasNameLength = strlen(biasName);
	// +3 for Cx_, +1 for closing /, +1 for terminating NUL char.
	char biasNameWithCore[biasNameLength + 3 + 1 + 1];

	biasNameWithCore[0] = 'C';
	if (coreId == 0)
		biasNameWithCore[1] = '0';
	else if (coreId == 1)
		biasNameWithCore[1] = '1';
	else if (coreId == 2)
		biasNameWithCore[1] = '2';
	else if (coreId == 3)
		biasNameWithCore[1] = '3';
	biasNameWithCore[2] = '_';

	strcpy(&biasNameWithCore[3], biasName);

	biasNameWithCore[biasNameLength + 3] = '/';
	biasNameWithCore[biasNameLength + 3 + 1] = '\0';

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNodeLP = sshsGetRelativeNode(dynapseNode, chipIDToName(chipId, true));

	sshsNode biasNodeLP = sshsGetRelativeNode(deviceConfigNodeLP, "bias/");

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNodeLP, biasNameWithCore);

	// Write bias settings.
	sshsNodePutByte(biasConfigNode, "coarseValue", I8T(coarseValue));
	sshsNodePutShort(biasConfigNode, "fineValue", I16T(fineValue));
	sshsNodePutString(biasConfigNode, "currentLevel", (highLow) ? ("High") : ("Low"));
}

/**
 * Get the current value of a certain bias of a specific core of a chip of the Dynap-SE device.
 *
 * @param dynapseNode Dynap-SE module configuration node (source node).
 * @param chipId Chip ID.
 * @param coreId Core ID.
 * @param biasName Bias name, like "IF_RFR_N" or "IF_DC_P".
 * @param coarseValue coarse current value, range [0,7], 0 is highest current, 7 lowest.
 * @param fineValue fine current value, range [0,255], 0 is lowest current, 255 highest.
 * @param highLow bias current level, choices are 'High' (true) and 'Low' (false).
 */
static inline void caerDynapseGetBiasCore(sshsNode dynapseNode, uint8_t chipId, uint8_t coreId, const char *biasName,
	uint8_t *coarseValue, uint8_t *fineValue, bool *highLow) {
	// Check if the pointer is valid.
	if (dynapseNode == NULL) {
		return;
	}

	if (chipId >= 4) {
		caerLog(CAER_LOG_ERROR, __func__, "Chip ID %d is invalid.", chipId);
		return;
	}

	if (coreId >= 4) {
		caerLog(CAER_LOG_ERROR, __func__, "Core ID %d is invalid.", coreId);
		return;
	}

	size_t biasNameLength = strlen(biasName);
	// +3 for Cx_, +1 for closing /, +1 for terminating NUL char.
	char biasNameWithCore[biasNameLength + 3 + 1 + 1];

	biasNameWithCore[0] = 'C';
	if (coreId == 0)
		biasNameWithCore[1] = '0';
	else if (coreId == 1)
		biasNameWithCore[1] = '1';
	else if (coreId == 2)
		biasNameWithCore[1] = '2';
	else if (coreId == 3)
		biasNameWithCore[1] = '3';
	biasNameWithCore[2] = '_';

	strcpy(&biasNameWithCore[3], biasName);

	biasNameWithCore[biasNameLength + 3] = '/';
	biasNameWithCore[biasNameLength + 3 + 1] = '\0';

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNodeLP = sshsGetRelativeNode(dynapseNode, chipIDToName(chipId, true));

	sshsNode biasNodeLP = sshsGetRelativeNode(deviceConfigNodeLP, "bias/");

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNodeLP, biasNameWithCore);

	// Read bias settings.
	int8_t coarse = sshsNodeGetByte(biasConfigNode, "coarseValue");
	if (coarseValue != NULL) {
		*coarseValue = U8T(coarse);
	}

	int16_t fine = sshsNodeGetShort(biasConfigNode, "fineValue");
	if (fineValue != NULL) {
		*fineValue = U8T(fine);
	}

	char *highLowStr = sshsNodeGetString(biasConfigNode, "currentLevel");
	if (highLow != NULL) {
		*highLow = caerStrEquals(highLowStr, "High");
	}
	free(highLowStr);
}

#ifdef __cplusplus
}
#endif

#endif /* MODULES_INI_DYNAPSE_UTILS_H_ */
