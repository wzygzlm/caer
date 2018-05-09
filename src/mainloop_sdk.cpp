#include "mainloop.h"

static MainloopData *glMainloopDataPtr;

void caerMainloopSDKLibInit(MainloopData *setMainloopPtr) {
	glMainloopDataPtr = setMainloopPtr;
}

void caerMainloopDataNotifyIncrease(void *p) {
	UNUSED_ARGUMENT(p);

	glMainloopDataPtr->dataAvailable.fetch_add(1, std::memory_order_release);
}

void caerMainloopDataNotifyDecrease(void *p) {
	UNUSED_ARGUMENT(p);

	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	glMainloopDataPtr->dataAvailable.fetch_sub(1, std::memory_order_relaxed);
}

bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId) {
	return (findBool(glMainloopDataPtr->streams.cbegin(), glMainloopDataPtr->streams.cend(),
		ActiveStreams(sourceId, typeId)));
}

bool caerMainloopModuleExists(int16_t id) {
	return (glMainloopDataPtr->modules.count(id) == 1);
}

enum caer_module_type caerMainloopModuleGetType(int16_t id) {
	return (glMainloopDataPtr->modules.at(id).libraryInfo->type);
}

sshsNode caerMainloopModuleGetConfigNode(int16_t id) {
	return (glMainloopDataPtr->modules.at(id).runtimeData->moduleNode);
}

size_t caerMainloopModuleGetInputDeps(int16_t id, int16_t **inputDepIds) {
	// Ensure is set to NULL for error return.
	// Support passing in NULL directly if not interested in result.
	if (inputDepIds != nullptr) {
		*inputDepIds = nullptr;
	}

	// Only makes sense to be called from PROCESSORs or OUTPUTs, as INPUTs
	// do not have inputs themselves.
	if (caerMainloopModuleGetType(id) == CAER_MODULE_INPUT) {
		return (0);
	}

	std::vector<int16_t> inputModuleIds(glMainloopDataPtr->modules.at(id).inputDefinition.size());

	// Get all module IDs of inputs to this module (each present only once in
	// 'inputDefinition' of module), then sort them and return if so requested.
	for (auto &in : glMainloopDataPtr->modules.at(id).inputDefinition) {
		inputModuleIds.push_back(in.first);
	}

	std::sort(inputModuleIds.begin(), inputModuleIds.end());

	if (inputDepIds != nullptr && !inputModuleIds.empty()) {
		*inputDepIds = (int16_t *) malloc(inputModuleIds.size() * sizeof(int16_t));
		if (*inputDepIds == nullptr) {
			// Memory allocation failure, report as if nothing found.
			return (0);
		}

		memcpy(*inputDepIds, inputModuleIds.data(), inputModuleIds.size() * sizeof(int16_t));
	}

	return (inputModuleIds.size());
}

size_t caerMainloopModuleGetOutputRevDeps(int16_t id, int16_t **outputRevDepIds) {
	// Ensure is set to NULL for error return.
	// Support passing in NULL directly if not interested in result.
	if (outputRevDepIds != nullptr) {
		*outputRevDepIds = nullptr;
	}

	// Only makes sense to be called from INPUTs or PROCESSORs, as OUTPUTs
	// do not have outputs themselves.
	if (caerMainloopModuleGetType(id) == CAER_MODULE_OUTPUT) {
		return (0);
	}

	// TODO: *.

	return (0);
}

void caerMainloopModuleResetOutputRevDeps(int16_t sourceID) {
	// TODO: *.
}

static inline caerModuleData caerMainloopGetSourceData(int16_t sourceID) {
	// Sources must be INPUTs or PROCESSORs.
	if (caerMainloopModuleGetType(sourceID) == CAER_MODULE_OUTPUT) {
		return (nullptr);
	}

	return (glMainloopDataPtr->modules.at(sourceID).runtimeData);
}

sshsNode caerMainloopGetSourceNode(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleNode);
}

void *caerMainloopGetSourceState(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleState);
}

sshsNode caerMainloopGetSourceInfo(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	// All sources should have a sub-node in SSHS called 'sourceInfo/',
	// while they are running only (so check running and existence).
	if (moduleData->moduleStatus == CAER_MODULE_STOPPED) {
		return (nullptr);
	}

	if (!sshsExistsRelativeNode(moduleData->moduleNode, "sourceInfo/")) {
		return (nullptr);
	}

	return (sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/"));
}

void caerMainloopResetInputs(int16_t sourceID) {
	for (auto &m : glMainloopDataPtr->globalExecution) {
		if (m.get().libraryInfo->type == CAER_MODULE_INPUT
			&& m.get().runtimeData->moduleStatus == CAER_MODULE_RUNNING) {
			m.get().runtimeData->doReset.store(sourceID);
		}
	}
}

void caerMainloopResetOutputs(int16_t sourceID) {
	for (auto &m : glMainloopDataPtr->globalExecution) {
		if (m.get().libraryInfo->type == CAER_MODULE_OUTPUT
			&& m.get().runtimeData->moduleStatus == CAER_MODULE_RUNNING) {
			m.get().runtimeData->doReset.store(sourceID);
		}
	}
}

void caerMainloopResetProcessors(int16_t sourceID) {
	for (auto &m : glMainloopDataPtr->globalExecution) {
		if (m.get().libraryInfo->type == CAER_MODULE_PROCESSOR
			&& m.get().runtimeData->moduleStatus == CAER_MODULE_RUNNING) {
			m.get().runtimeData->doReset.store(sourceID);
		}
	}
}
