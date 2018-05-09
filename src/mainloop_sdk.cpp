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

}

size_t caerMainloopModuleGetOutputRevDeps(int16_t id, int16_t **outputRevDepIds) {

}

void caerMainloopModuleResetOutputRevDeps(int16_t sourceID) {

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
