#include "mainloop.h"
#include "lib/utils.h"
#include "lib/cross/pathmax.h"
#include <csignal>

#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>

#include <boost/filesystem.hpp>
#include <boost/range/join.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

void caerMainloopDataNotifyIncrease(void *p) {
	UNUSED_ARGUMENT(p);

	glMainloopData.dataAvailable.fetch_add(1, std::memory_order_release);
}

void caerMainloopDataNotifyDecrease(void *p) {
	UNUSED_ARGUMENT(p);

	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	glMainloopData.dataAvailable.fetch_sub(1, std::memory_order_relaxed);
}

bool caerMainloopModuleExists(int16_t id) {
	return (glMainloopData.modules.count(id) == 1);
}

bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type) {
	return (glMainloopData.modules.at(id).libraryInfo->type == type);
}

bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId) {
	return (findBool(glMainloopData.streams.begin(), glMainloopData.streams.end(), ActiveStreams(sourceId, typeId)));
}

int16_t *caerMainloopGetModuleInputIDs(int16_t id, size_t *inputsSize) {
	// If inputsSize is known, allow not passing it in.
	if (inputsSize != nullptr) {
		*inputsSize = 0;
	}

	// Only makes sense to be called from PROCESSORs or OUTPUTs, as INPUTs
	// do not have inputs themselves.
	if (caerMainloopModuleIsType(id, CAER_MODULE_INPUT)) {
		return (nullptr);
	}

	size_t inDefSize = glMainloopData.modules.at(id).inputDefinition.size();

	int16_t *inputs = (int16_t *) malloc(inDefSize * sizeof(int16_t));
	if (inputs == nullptr) {
		return (nullptr);
	}

	size_t idx = 0;
	for (auto inDef : glMainloopData.modules.at(id).inputDefinition) {
		inputs[idx++] = inDef.first;
	}

	if (inputsSize != nullptr) {
		*inputsSize = idx;
	}
	return (inputs);
}

static inline caerModuleData caerMainloopGetSourceData(int16_t sourceID) {
	caerModuleData moduleData = glMainloopData.modules.at(sourceID).runtimeData;
	if (moduleData == nullptr) {
		return (nullptr);
	}

	// Sources must be INPUTs or PROCESSORs.
	if (caerMainloopModuleIsType(sourceID, CAER_MODULE_OUTPUT)) {
		return (nullptr);
	}

	return (moduleData);
}

sshsNode caerMainloopGetSourceNode(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleNode);
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

void *caerMainloopGetSourceState(int16_t sourceID) {
	caerModuleData moduleData = caerMainloopGetSourceData(sourceID);
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleState);
}

sshsNode caerMainloopGetModuleNode(int16_t sourceID) {
	caerModuleData moduleData = glMainloopData.modules.at(sourceID).runtimeData;
	if (moduleData == nullptr) {
		return (nullptr);
	}

	return (moduleData->moduleNode);
}

void caerMainloopResetInputs(int16_t sourceID) {
	for (auto &m : glMainloopData.globalExecution) {
		if (m.get().libraryInfo->type == CAER_MODULE_INPUT) {
			m.get().runtimeData->doReset.store(sourceID);
		}
	}
}

void caerMainloopResetOutputs(int16_t sourceID) {
	for (auto &m : glMainloopData.globalExecution) {
		if (m.get().libraryInfo->type == CAER_MODULE_OUTPUT) {
			m.get().runtimeData->doReset.store(sourceID);
		}
	}
}

void caerMainloopResetProcessors(int16_t sourceID) {
	for (auto &m : glMainloopData.globalExecution) {
		if (m.get().libraryInfo->type == CAER_MODULE_PROCESSOR) {
			m.get().runtimeData->doReset.store(sourceID);
		}
	}
}
