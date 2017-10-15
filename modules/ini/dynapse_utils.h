#ifndef MODULES_INI_DYNAPSE_UTILS_H_
#define MODULES_INI_DYNAPSE_UTILS_H_

#include "main.h"
#include <libcaer/devices/dynapse.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const char *chipIDToName(uint8_t chipID, bool withEndSlash) {
	switch (chipID) {
		case DYNAPSE_CONFIG_DYNAPSE_U0: {
			return ((withEndSlash) ? ("U0/") : ("U0"));
			break;
		}
		case DYNAPSE_CONFIG_DYNAPSE_U1: {
			return ((withEndSlash) ? ("U1/") : ("U1"));
			break;
		}
		case DYNAPSE_CONFIG_DYNAPSE_U2: {
			return ((withEndSlash) ? ("U2/") : ("U2"));
			break;
		}
		case DYNAPSE_CONFIG_DYNAPSE_U3: {
			return ((withEndSlash) ? ("U3/") : ("U3"));
			break;
		}
		case DYNAPSE_CHIP_DYNAPSE: {
			return ((withEndSlash) ? ("DYNAPSE/") : ("DYNAPSE"));
			break;
		}
	}

	return ((withEndSlash) ? ("Unsupported/") : ("Unsupported"));
}

static inline const char *coreIDToName(uint8_t coreID, bool withEndSlash) {
	switch (coreID) {
		case 0: {
			return ((withEndSlash) ? ("C0/") : ("C0"));
			break;
		}
		case 1: {
			return ((withEndSlash) ? ("C1/") : ("C1"));
			break;
		}
		case 2: {
			return ((withEndSlash) ? ("C2/") : ("C2"));
			break;
		}
		case 3: {
			return ((withEndSlash) ? ("C3/") : ("C3"));
			break;
		}
	}

	return ((withEndSlash) ? ("Unsupported/") : ("Unsupported"));
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

	// Biases are in their own sub-nodes. Generate full path.
	size_t nodePathLength = (size_t) snprintf(NULL, 0, "bias/U%d/C%d/%s/", chipId, coreId, biasName);

	char nodePath[nodePathLength + 1];
	snprintf(nodePath, nodePathLength + 1, "bias/U%d/C%d/%s/", chipId, coreId, biasName);
	nodePath[nodePathLength] = '\0';

	// Get configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(dynapseNode, nodePath);

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

	// Biases are in their own sub-nodes. Generate full path.
	size_t nodePathLength = (size_t) snprintf(NULL, 0, "bias/U%d/C%d/%s/", chipId, coreId, biasName);

	char nodePath[nodePathLength + 1];
	snprintf(nodePath, nodePathLength + 1, "bias/U%d/C%d/%s/", chipId, coreId, biasName);
	nodePath[nodePathLength] = '\0';

	// Get configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(dynapseNode, nodePath);

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
