#ifndef DYNAPSE_COMMON_H_
#define DYNAPSE_COMMON_H_

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/special.h>
#include <libcaer/events/spike.h>
#include <libcaer/devices/dynapse.h>

#ifdef HAVE_PTHREADS
#include "ext/c11threads_posix.h"
#endif

#include <math.h>
#include <stdatomic.h>
#include <sys/types.h>

struct gen_spike_state {
	atomic_bool doStim;
	atomic_int_fast32_t stim_type;
	atomic_int_fast32_t stim_avr;				// Hertz [1/s]
	atomic_int_fast32_t stim_std;				//
	atomic_int_fast32_t stim_duration;
	atomic_bool repeat;
	atomic_bool teaching;
	atomic_bool sendTeachingStimuli;
	atomic_bool sendInhibitoryStimuli;
	atomic_bool setCam;
	atomic_bool setCamSingle;
	atomic_bool clearCam;
	atomic_bool clearAllCam;
	atomic_bool doStimPrimitiveBias;
	atomic_bool doStimPrimitiveCam;
	atomic_bool loadDefaultBiases;
	atomic_bool done;
	atomic_bool started;
	thrd_t spikeGenThread;
	atomic_bool running;
	/*address spike*/
	atomic_int_fast32_t core_d;
	atomic_int_fast32_t address;
	atomic_int_fast32_t core_s;
	atomic_int_fast32_t chip_id;
	atomic_int_fast32_t dx;
	atomic_int_fast32_t dy;
	atomic_bool sx;
	atomic_bool sy;
	/* ETF */
	// stimulation Thread ETF
	atomic_bool ETFstarted;
	atomic_bool ETFdone;
	atomic_int_fast32_t ETFchip_id;		// the chip that will be measured [0,4,8,12]
	atomic_int_fast32_t ETFduration;	// total stimulation duration
	atomic_int_fast32_t ETFphase_num;	// stimulation phase number
	atomic_bool ETFrepeat;
	int ETFstepnum;
};

// TODO: this should be private. gen_spikes.c should be in the main C file.
struct caer_input_dynapse_state {
	caerDeviceHandle deviceState;
	sshsNode eventSourceConfigNode;
	struct gen_spike_state genSpikeState;
};

typedef struct caer_input_dynapse_state *caerInputDynapseState;

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

	printf("unknown device id %d exiting...\n", chipID);
	exit(1);
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

	if (chipId != DYNAPSE_CONFIG_DYNAPSE_U0 && chipId != DYNAPSE_CONFIG_DYNAPSE_U1
		&& chipId != DYNAPSE_CONFIG_DYNAPSE_U2 && chipId != DYNAPSE_CONFIG_DYNAPSE_U3) {
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

	if (chipId != DYNAPSE_CONFIG_DYNAPSE_U0 && chipId != DYNAPSE_CONFIG_DYNAPSE_U1
		&& chipId != DYNAPSE_CONFIG_DYNAPSE_U2 && chipId != DYNAPSE_CONFIG_DYNAPSE_U3) {
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

bool caerGenSpikeInit(caerModuleData moduleData);
void caerGenSpikeExit(caerModuleData moduleData);

#endif /* DYNAPSE_COMMON_H_ */
