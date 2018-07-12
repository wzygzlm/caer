#include "sshs_internal.hpp"
#include <boost/tokenizer.hpp>
#include <iostream>
#include <mutex>
#include <regex>

struct sshs_struct {
	sshsNode root;
};

static void sshsGlobalInitialize(void);
static void sshsGlobalErrorLogCallbackInitialize(void);
static void sshsGlobalErrorLogCallbackSetInternal(sshsErrorLogCallback error_log_cb);
static void sshsDefaultErrorLogCallback(const char *msg);
static bool sshsCheckAbsoluteNodePath(const std::string &absolutePath);
static bool sshsCheckRelativeNodePath(const std::string &relativePath);

static sshs sshsGlobal = nullptr;
static std::once_flag sshsGlobalIsInitialized;

static void sshsGlobalInitialize(void) {
	sshsGlobal = sshsNew();
}

sshs sshsGetGlobal(void) {
	std::call_once(sshsGlobalIsInitialized, &sshsGlobalInitialize);

	return (sshsGlobal);
}

static sshsErrorLogCallback sshsGlobalErrorLogCallback = nullptr;
static std::once_flag sshsGlobalErrorLogCallbackIsInitialized;

static void sshsGlobalErrorLogCallbackInitialize(void) {
	sshsGlobalErrorLogCallbackSetInternal(&sshsDefaultErrorLogCallback);
}

static void sshsGlobalErrorLogCallbackSetInternal(sshsErrorLogCallback error_log_cb) {
	sshsGlobalErrorLogCallback = error_log_cb;
}

sshsErrorLogCallback sshsGetGlobalErrorLogCallback(void) {
	std::call_once(sshsGlobalErrorLogCallbackIsInitialized, &sshsGlobalErrorLogCallbackInitialize);

	return (sshsGlobalErrorLogCallback);
}

/**
 * This is not thread-safe, and it's not meant to be.
 * Set the global error callback preferably only once, before using SSHS.
 */
void sshsSetGlobalErrorLogCallback(sshsErrorLogCallback error_log_cb) {
	std::call_once(sshsGlobalErrorLogCallbackIsInitialized, &sshsGlobalErrorLogCallbackInitialize);

	// If nullptr, set to default logging callback.
	if (error_log_cb == nullptr) {
		sshsGlobalErrorLogCallbackSetInternal(&sshsDefaultErrorLogCallback);
	}
	else {
		sshsGlobalErrorLogCallbackSetInternal(error_log_cb);
	}
}

sshs sshsNew(void) {
	sshs newSshs = (sshs) malloc(sizeof(*newSshs));
	sshsMemoryCheck(newSshs, __func__);

	// Create root node.
	newSshs->root = sshsNodeNew("", nullptr);

	return (newSshs);
}

bool sshsExistsNode(sshs st, const char *nodePathC) {
	const std::string nodePath(nodePathC);

	if (!sshsCheckAbsoluteNodePath(nodePath)) {
		errno = EINVAL;
		return (false);
	}

	// First node is the root.
	sshsNode curr = st->root;

	// Optimization: the root node always exists.
	if (nodePath == "/") {
		return (true);
	}

	boost::tokenizer<boost::char_separator<char>> nodePathTokens(nodePath, boost::char_separator<char>("/"));

	// Search (or create) viable node iteratively.
	for (const auto &tok : nodePathTokens) {
		sshsNode next = sshsNodeGetChild(curr, tok.c_str());

		// If node doesn't exist, return that.
		if (next == nullptr) {
			errno = ENOENT;
			return (false);
		}

		curr = next;
	}

	// We got to the end, so the node exists.
	return (true);
}

sshsNode sshsGetNode(sshs st, const char *nodePathC) {
	const std::string nodePath(nodePathC);

	if (!sshsCheckAbsoluteNodePath(nodePath)) {
		errno = EINVAL;
		return (nullptr);
	}

	// First node is the root.
	sshsNode curr = st->root;

	// Optimization: the root node always exists and is right there.
	if (nodePath == "/") {
		return (curr);
	}

	boost::tokenizer<boost::char_separator<char>> nodePathTokens(nodePath, boost::char_separator<char>("/"));

	// Search (or create) viable node iteratively.
	for (const auto &tok : nodePathTokens) {
		sshsNode next = sshsNodeGetChild(curr, tok.c_str());

		// Create next node in path if not existing.
		if (next == nullptr) {
			next = sshsNodeAddChild(curr, tok.c_str());
		}

		curr = next;
	}

	// 'curr' now contains the specified node.
	return (curr);
}

bool sshsExistsRelativeNode(sshsNode node, const char *nodePathC) {
	const std::string nodePath(nodePathC);

	if (!sshsCheckRelativeNodePath(nodePath)) {
		errno = EINVAL;
		return (false);
	}

	// Start with the given node.
	sshsNode curr = node;

	boost::tokenizer<boost::char_separator<char>> nodePathTokens(nodePath, boost::char_separator<char>("/"));

	// Search (or create) viable node iteratively.
	for (const auto &tok : nodePathTokens) {
		sshsNode next = sshsNodeGetChild(curr, tok.c_str());

		// If node doesn't exist, return that.
		if (next == nullptr) {
			errno = ENOENT;
			return (false);
		}

		curr = next;
	}

	// We got to the end, so the node exists.
	return (true);
}

sshsNode sshsGetRelativeNode(sshsNode node, const char *nodePathC) {
	const std::string nodePath(nodePathC);

	if (!sshsCheckRelativeNodePath(nodePath)) {
		errno = EINVAL;
		return (nullptr);
	}

	// Start with the given node.
	sshsNode curr = node;

	boost::tokenizer<boost::char_separator<char>> nodePathTokens(nodePath, boost::char_separator<char>("/"));

	// Search (or create) viable node iteratively.
	for (const auto &tok : nodePathTokens) {
		sshsNode next = sshsNodeGetChild(curr, tok.c_str());

		// Create next node in path if not existing.
		if (next == nullptr) {
			next = sshsNodeAddChild(curr, tok.c_str());
		}

		curr = next;
	}

	// 'curr' now contains the specified node.
	return (curr);
}

bool sshsBeginTransaction(sshs st, const char *nodePaths[], size_t nodePathsLength) {
	// Check all node paths, then lock them.
	for (size_t i = 0; i < nodePathsLength; i++) {
		if (!sshsCheckAbsoluteNodePath(nodePaths[i])) {
			errno = EINVAL;
			return (false);
		}
	}

	for (size_t i = 0; i < nodePathsLength; i++) {
		sshsNodeTransactionLock(sshsGetNode(st, nodePaths[i]));
	}

	return (true);
}

bool sshsEndTransaction(sshs st, const char *nodePaths[], size_t nodePathsLength) {
	// Check all node paths, then unlock them.
	for (size_t i = 0; i < nodePathsLength; i++) {
		if (!sshsCheckAbsoluteNodePath(nodePaths[i])) {
			errno = EINVAL;
			return (false);
		}
	}

	for (size_t i = 0; i < nodePathsLength; i++) {
		sshsNodeTransactionUnlock(sshsGetNode(st, nodePaths[i]));
	}

	return (true);
}

#define ALLOWED_CHARS_REGEXP "([a-zA-Z-_\\d\\.]+/)"
static const std::regex sshsAbsoluteNodePathRegexp("^/" ALLOWED_CHARS_REGEXP "*$");
static const std::regex sshsRelativeNodePathRegexp("^" ALLOWED_CHARS_REGEXP "+$");

static bool sshsCheckAbsoluteNodePath(const std::string &absolutePath) {
	if (absolutePath.empty()) {
		(*sshsGetGlobalErrorLogCallback())("Absolute node path cannot be empty.");
		return (false);
	}

	if (!std::regex_match(absolutePath, sshsAbsoluteNodePathRegexp)) {
		boost::format errorMsg = boost::format("Invalid absolute node path format: '%s'.") % absolutePath;

		(*sshsGetGlobalErrorLogCallback())(errorMsg.str().c_str());

		return (false);
	}

	return (true);
}

static bool sshsCheckRelativeNodePath(const std::string &relativePath) {
	if (relativePath.empty()) {
		(*sshsGetGlobalErrorLogCallback())("Relative node path cannot be empty.");
		return (false);
	}

	if (!std::regex_match(relativePath, sshsRelativeNodePathRegexp)) {
		boost::format errorMsg = boost::format("Invalid relative node path format: '%s'.") % relativePath;

		(*sshsGetGlobalErrorLogCallback())(errorMsg.str().c_str());

		return (false);
	}

	return (true);
}

static void sshsDefaultErrorLogCallback(const char *msg) {
	std::cerr << msg << std::endl;
}
