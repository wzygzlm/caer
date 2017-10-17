#include "sshs_internal.h"
#include <float.h>

#include <vector>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <algorithm>
#include <boost/variant.hpp>
#include <boost/format.hpp>

// We don't care about unlocking anything here, as we exit hard on error anyway.
static inline void sshsNodeError(const std::string &funcName, const std::string &key,
	enum sshs_node_attr_value_type type, const std::string &msg) {
	boost::format errorMsg = boost::format("%s(): attribute '%s' (type '%s'): %s.") % funcName % key
		% sshsHelperTypeToStringConverter(type) % msg;

	(*sshsGetGlobalErrorLogCallback())(errorMsg.str().c_str());

	// This is a critical usage error that *must* be fixed!
	exit(EXIT_FAILURE);
}

static inline void sshsNodeErrorNoAttribute(const std::string &funcName, const std::string &key,
	enum sshs_node_attr_value_type type) {
	sshsNodeError(funcName, key, type, "attribute doesn't exist, you must create it first");
}

template<typename InIter, typename Elem>
static inline bool findBool(InIter begin, InIter end, const Elem &val) {
	const auto result = std::find(begin, end, val);

	if (result == end) {
		return (false);
	}

	return (true);
}

class sshs_node_attr {
public:
	union sshs_node_attr_range min;
	union sshs_node_attr_range max;
	int flags;
	std::string description;
	sshs_value value;
};

class sshs_node_listener {
private:
	sshsNodeChangeListener nodeChanged;
	void *userData;

public:
	sshs_node_listener(sshsNodeChangeListener _listener, void *_userData) :
			nodeChanged(_listener),
			userData(_userData) {
	}

	sshsNodeChangeListener getListener() const noexcept {
		return (nodeChanged);
	}

	void *getUserData() const noexcept {
		return (userData);
	}

	// Comparison operators.
	bool operator==(const sshs_node_listener &rhs) const noexcept {
		return ((nodeChanged == rhs.nodeChanged) && (userData == rhs.userData));
	}

	bool operator!=(const sshs_node_listener &rhs) const noexcept {
		return ((nodeChanged != rhs.nodeChanged) || (userData != rhs.userData));
	}
};

class sshs_node_attr_listener {
private:
	sshsAttributeChangeListener attributeChanged;
	void *userData;

public:
	sshs_node_attr_listener(sshsAttributeChangeListener _listener, void *_userData) :
			attributeChanged(_listener),
			userData(_userData) {
	}

	sshsAttributeChangeListener getListener() const noexcept {
		return (attributeChanged);
	}

	void *getUserData() const noexcept {
		return (userData);
	}

	// Comparison operators.
	bool operator==(const sshs_node_attr_listener &rhs) const noexcept {
		return ((attributeChanged == rhs.attributeChanged) && (userData == rhs.userData));
	}

	bool operator!=(const sshs_node_attr_listener &rhs) const noexcept {
		return ((attributeChanged != rhs.attributeChanged) || (userData != rhs.userData));
	}
};

// struct for C compatibility
struct sshs_node {
public:
	std::string name;
	std::string path;
	sshsNode parent;
	std::map<std::string, sshsNode> children;
	std::map<std::string, sshs_node_attr> attributes;
	std::vector<sshs_node_listener> nodeListeners;
	std::vector<sshs_node_attr_listener> attrListeners;
	std::shared_timed_mutex traversal_lock;
	std::recursive_mutex node_lock;

	sshs_node(std::string _name, sshsNode _parent) :
			name(_name),
			parent(_parent) {
		// Path is based on parent.
		if (_parent != nullptr) {
			path = parent->path + _name + "/";
		}
		else {
			// Or the root has an empty, constant path.
			path = "/";
		}
	}
};

static void sshsNodeDestroy(sshsNode node);
static void sshsNodeRemoveChild(sshsNode node, const char *childName);
static void sshsNodeRemoveAllChildren(sshsNode node);
static void sshsNodeRemoveSubTree(sshsNode node);
static const char *sshsNodeXMLWhitespaceCallback(mxml_node_t *node, int where);
static void sshsNodeToXML(sshsNode node, int outFd, bool recursive);
static mxml_node_t *sshsNodeGenerateXML(sshsNode node, bool recursive);
static mxml_node_t **sshsNodeXMLFilterChildNodes(mxml_node_t *node, const char *nodeName, size_t *numChildren);
static bool sshsNodeFromXML(sshsNode node, int inFd, bool recursive, bool strict);
static void sshsNodeConsumeXML(sshsNode node, mxml_node_t *content, bool recursive);

sshsNode sshsNodeNew(const char *nodeName, sshsNode parent) {
	sshsNode newNode = new sshs_node(nodeName, parent);
	SSHS_MALLOC_CHECK_EXIT(newNode);

	return (newNode);
}

// children, attributes, and listeners must be cleaned up prior to this call.
static void sshsNodeDestroy(sshsNode node) {
	delete node;
}

const char *sshsNodeGetName(sshsNode node) {
	return (node->name.c_str());
}

const char *sshsNodeGetPath(sshsNode node) {
	return (node->path.c_str());
}

sshsNode sshsNodeGetParent(sshsNode node) {
	return (node->parent);
}

sshsNode sshsNodeAddChild(sshsNode node, const char *childName) {
	std::unique_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	// Atomic putIfAbsent: returns null if nothing was there before and the
	// node is the new one, or it returns the old node if already present.
	if (node->children.count(childName)) {
		return (node->children[childName]);
	}
	else {
		// Create new child node with appropriate name and parent.
		sshsNode newChild = sshsNodeNew(childName, node);

		// No node present, let's add it.
		node->children[childName] = newChild;

		// Listener support (only on new addition!).
		std::lock_guard<std::recursive_mutex> nodeLock(node->node_lock);

		for (const auto &l : node->nodeListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_ADDED, childName);
		}

		return (newChild);
	}
}

sshsNode sshsNodeGetChild(sshsNode node, const char* childName) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	if (node->children.count(childName)) {
		return (node->children[childName]);
	}
	else {
		return (nullptr);
	}
}

sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	size_t childrenCount = node->children.size();

	// If none, exit gracefully.
	if (childrenCount == 0) {
		*numChildren = 0;
		return (nullptr);
	}

	sshsNode *children = (sshsNode *) malloc(childrenCount * sizeof(*children));
	SSHS_MALLOC_CHECK_EXIT(children);

	size_t i = 0;
	for (const auto &n : node->children) {
		children[i++] = n.second;
	}

	*numChildren = childrenCount;
	return (children);
}

void sshsNodeAddNodeListener(sshsNode node, void *userData, sshsNodeChangeListener node_changed) {
	sshs_node_listener listener(node_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	if (!findBool(node->nodeListeners.begin(), node->nodeListeners.end(), listener)) {
		node->nodeListeners.push_back(listener);
	}
}

void sshsNodeRemoveNodeListener(sshsNode node, void *userData, sshsNodeChangeListener node_changed) {
	sshs_node_listener listener(node_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	std::remove(node->nodeListeners.begin(), node->nodeListeners.end(), listener);
}

void sshsNodeRemoveAllNodeListeners(sshsNode node) {
	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	node->nodeListeners.clear();
}

void sshsNodeAddAttributeListener(sshsNode node, void *userData, sshsAttributeChangeListener attribute_changed) {
	sshs_node_attr_listener listener(attribute_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	if (!findBool(node->attrListeners.begin(), node->attrListeners.end(), listener)) {
		node->attrListeners.push_back(listener);
	}
}

void sshsNodeRemoveAttributeListener(sshsNode node, void *userData, sshsAttributeChangeListener attribute_changed) {
	sshs_node_attr_listener listener(attribute_changed, userData);

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	std::remove(node->attrListeners.begin(), node->attrListeners.end(), listener);
}

void sshsNodeRemoveAllAttributeListeners(sshsNode node) {
	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	node->attrListeners.clear();
}

void sshsNodeTransactionLock(sshsNode node) {
	node->node_lock.lock();
}

void sshsNodeTransactionUnlock(sshsNode node) {
	node->node_lock.unlock();
}

void sshsNodeCreateAttribute(sshsNode node, const std::string &key, const sshs_value &defaultValue,
	const struct sshs_node_attr_ranges &ranges, int flags, const std::string &description) {
	// Parse range struct.
	union sshs_node_attr_range minValue = ranges.min;
	union sshs_node_attr_range maxValue = ranges.max;

	// Strings are special, their length range goes from 0 to SIZE_MAX, but we
	// have to restrict that to from 0 to INT32_MAX for languages like Java
	// that only support integer string lengths. It's also reasonable.
	if (defaultValue.getType() == SSHS_STRING) {
		if (minValue.stringRange > INT32_MAX) {
			char errorMsg[1024];
			snprintf(errorMsg, 1024,
				"sshsNodeCreateAttribute(): attribute '%s' of type 'string' has a minimum range value of '%" PRIi64 "' outside allowed limits. "
				"Please make sure the value is positive, between 0 and %" PRIi32 "!", key.c_str(), minValue.stringRange,
				INT32_MAX);

			(*sshsGetGlobalErrorLogCallback())(errorMsg);

			// This is a critical usage error that *must* be fixed!
			exit(EXIT_FAILURE);
		}

		if (maxValue.stringRange > INT32_MAX) {
			char errorMsg[1024];
			snprintf(errorMsg, 1024,
				"sshsNodeCreateAttribute(): attribute '%s' of type 'string' has a maximum range value of '%" PRIi64 "' outside allowed limits. "
				"Please make sure the value is positive, between 0 and %" PRIi32 "!", key.c_str(), maxValue.stringRange,
				INT32_MAX);

			(*sshsGetGlobalErrorLogCallback())(errorMsg);

			// This is a critical usage error that *must* be fixed!
			exit(EXIT_FAILURE);
		}
	}

	// Check that value conforms to range limits.
	if (!defaultValue.valueInRange(minValue, maxValue)) {
		// Fail on wrong default value. Must be within range!
		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodeCreateAttribute(): attribute '%s' of type '%s' has default value '%s' that is out of specified range. "
				"Please make sure the default value is within the given range!", key.c_str(),
			sshsHelperTypeToStringConverter(defaultValue.getType()), sshsHelperValueToStringConverter(defaultValue));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	// Restrict NOTIFY_ONLY flag to booleans only, for button-like behavior.
	if ((flags & SSHS_FLAGS_NOTIFY_ONLY) && defaultValue.getType() != SSHS_BOOL) {
		// Fail on wrong notify-only flag usage.
		char errorMsg[1024];
		snprintf(errorMsg, 1024, "Attribute '%s' of type '%s' has the NOTIFY_ONLY flag set, but is not of type BOOL. "
			"Only booleans may have this flag set!", key.c_str(),
			sshsHelperTypeToStringConverter(defaultValue.getType()));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	sshs_node_attr newAttr;

	newAttr.value = defaultValue;

	newAttr.min = minValue;
	newAttr.max = maxValue;
	newAttr.flags = flags;
	newAttr.description = description;

	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	// Add if not present. Else update value (below).
	if (!node->attributes.count(key)) {
		node->attributes[key] = newAttr;

		// Listener support. Call only on change, which is always the case here.
		for (const auto &l : node->attrListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_ATTRIBUTE_ADDED, key.c_str(), newAttr.value.getType(),
				newAttr.value.toCUnion(true));
		}
	}
	else {
		const sshs_value &oldAttrValue = node->attributes[key].value;

		if (oldAttrValue.getType() != newAttr.value.getType()) {
			throw new std::runtime_error("Called create with same key but different types, not supported.");
		}

		// Check if the current value is still fine and within range; if it is
		// we use it, else just use the new value.
		if (oldAttrValue.valueInRange(minValue, maxValue)) {
			// Only update value, then use newAttr. No listeners called since this
			// is by definition the old value and as such nothing can have changed.
			newAttr.value = oldAttrValue;
			node->attributes[key] = newAttr;
		}
		else {
			// If the old value is not in range anymore, the new value must be different,
			// since it is guaranteed to be inside the new range. So we call the listeners.
			node->attributes[key] = newAttr;

			// Listener support. Call only on change, which is always the case here.
			for (const auto &l : node->attrListeners) {
				(*l.getListener())(node, l.getUserData(), SSHS_ATTRIBUTE_MODIFIED, key.c_str(), newAttr.value.getType(),
					newAttr.value.toCUnion(true));
			}
		}
	}
}

void sshsNodeRemoveAttribute(sshsNode node, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	try {
		sshs_node_attr &attr = node->attributes.at(key);

		// Listener support.
		for (const auto &l : node->attrListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_ATTRIBUTE_REMOVED, key.c_str(), attr.value.getType(),
				attr.value.toCUnion(true));
		}
	}
	catch (const std::out_of_range &) {
		// Verify that a valid attribute exists, else simply return
		// without doing anything. Attribute was already deleted.
		return;
	}

	// Remove attribute from node.
	node->attributes.erase(key);
}

void sshsNodeRemoveAllAttributes(sshsNode node) {
	std::lock_guard<std::recursive_mutex> lock(node->node_lock);

	for (const auto &attr : node->attributes) {
		for (const auto &l : node->attrListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_ATTRIBUTE_REMOVED, attr.first.c_str(),
				attr.second.value.getType(), attr.second.value.toCUnion(true));
		}
	}

	node->attributes.clear();
}

void sshsNodeClearSubTree(sshsNode startNode, bool clearStartNode) {
	// Clear this node's attributes, if requested.
	if (clearStartNode) {
		sshsNodeRemoveAllAttributes(startNode);
		sshsNodeRemoveAllAttributeListeners(startNode);
	}

	// Recurse down children and remove all attributes.
	size_t numChildren;
	sshsNode *children = sshsNodeGetChildren(startNode, &numChildren);

	for (size_t i = 0; i < numChildren; i++) {
		sshsNodeClearSubTree(children[i], true);
	}

	free(children);
}

// children, attributes, and listeners for the child to be removed
// must be cleaned up prior to this call.
static void sshsNodeRemoveChild(sshsNode node, const char *childName) {
	std::unique_lock<std::shared_timed_mutex> lock(node->traversal_lock);
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	try {
		sshsNodeDestroy(node->children.at(childName));

		// Listener support.
		for (const auto &l : node->nodeListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_REMOVED, childName);
		}
	}
	catch (const std::out_of_range &) {
		// Verify that a valid attribute exists, else simply return
		// without doing anything. Attribute was already deleted.
		return;
	}

	// Remove attribute from node.
	node->children.erase(childName);
}

// children, attributes, and listeners for the children to be removed
// must be cleaned up prior to this call.
static void sshsNodeRemoveAllChildren(sshsNode node) {
	std::unique_lock<std::shared_timed_mutex> lock(node->traversal_lock);
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	for (const auto &child : node->children) {
		for (const auto &l : node->nodeListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_REMOVED, child.second->name.c_str());
		}

		sshsNodeDestroy(child.second);
	}

	node->children.clear();
}

static void sshsNodeRemoveSubTree(sshsNode node) {
	// Recurse down first, we remove from the bottom up.
	size_t numChildren;
	sshsNode *children = sshsNodeGetChildren(node, &numChildren);

	for (size_t i = 0; i < numChildren; i++) {
		sshsNodeRemoveSubTree(children[i]);
	}

	free(children);

	// Delete node listeners and children.
	sshsNodeRemoveAllChildren(node);
	sshsNodeRemoveAllNodeListeners(node);
}

// Eliminates this node and any children. Nobody can have a reference, or
// be in the process of getting one, to this node or any of its children.
// You need to make sure of this in your application!
void sshsNodeRemoveNode(sshsNode node) {
	// Now we can clear the subtree from all attribute related data.
	sshsNodeClearSubTree(node, true);

	// And finally remove the node related data and the node itself.
	sshsNodeRemoveSubTree(node);

	// If this is the root node (parent == nullptr), it isn't fully removed.
	if (sshsNodeGetParent(node) != nullptr) {
		// Unlink this node from the parent.
		// This also destroys the memory associated with the node.
		// Any later access is illegal!
		sshsNodeRemoveChild(sshsNodeGetParent(node), sshsNodeGetName(node));
	}
}

bool sshsNodeAttributeExists(sshsNode node, const std::string &key,
	enum sshs_node_attr_value_type type = SSHS_UNKNOWN) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if ((!node->attributes.count(key)) || (type != SSHS_UNKNOWN && node->attributes[key].value.getType() != type)) {
		errno = ENOENT;
		return (false);
	}

	// The specified attribute exists and has a matching type.
	return (true);
}

bool sshsNodePutAttribute(sshsNode node, const std::string &key, const sshs_value &value, bool forceReadOnlyUpdate =
	false) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key, value.getType())) {
		sshsNodeErrorNoAttribute("sshsNodePutAttribute", key, value.getType());
	}

	sshs_node_attr &attr = node->attributes[key];

	// Value must be present, so update old one, after checking range and flags.
	if ((!forceReadOnlyUpdate && (attr.flags & SSHS_FLAGS_READ_ONLY))
		|| (forceReadOnlyUpdate && (attr.flags & SSHS_FLAGS_READ_ONLY) == 0)) {
		// Read-only flag set, cannot put new value!
		errno = EPERM;
		return (false);
	}

	if (!value.valueInRange(attr.min, attr.max)) {
		// New value out of range, cannot put new value!
		errno = ERANGE;
		return (false);
	}

	// Key and valueType have to be the same, so only update the value
	// itself with the new one, and save the old one for later.
	const sshs_value attrValueOld = attr.value;

	attr.value = value;

	// Let's check if anything changed with this update and call
	// the appropriate listeners if needed.
	if (attrValueOld != attr.value) {
		// Listener support. Call only on change, which is always the case here.
		for (const auto &l : node->attrListeners) {
			(*l.getListener())(node, l.getUserData(), SSHS_ATTRIBUTE_MODIFIED, key.c_str(), attr.value.getType(),
				attr.value.toCUnion(true));
		}
	}

	return (true);
}

sshs_value sshsNodeGetAttribute(sshsNode node, const std::string &key, enum sshs_node_attr_value_type type =
	SSHS_UNKNOWN) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttribute", key, type);
	}

	// Return the final value.
	return (node->attributes[key].value);
}

void sshsNodeCreateBool(sshsNode node, const std::string &key, bool defaultValue, int flags,
	const std::string &description) {
	sshs_value uValue;
	uValue.setBool(defaultValue);

	// No range for booleans.
	struct sshs_node_attr_ranges ranges;
	ranges.min.ibyteRange = 0;
	ranges.max.ibyteRange = 0;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutBool(sshsNode node, const std::string &key, bool value) {
	sshs_value uValue;
	uValue.setBool(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

bool sshsNodeGetBool(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getBool());
}

void sshsNodeCreateByte(sshsNode node, const std::string &key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setByte(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ibyteRange = minValue;
	ranges.max.ibyteRange = maxValue;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutByte(sshsNode node, const std::string &key, int8_t value) {
	sshs_value uValue;
	uValue.setByte(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

int8_t sshsNodeGetByte(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getByte());
}

void sshsNodeCreateShort(sshsNode node, const std::string &key, int16_t defaultValue, int16_t minValue,
	int16_t maxValue, int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setShort(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ishortRange = minValue;
	ranges.max.ishortRange = maxValue;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutShort(sshsNode node, const std::string &key, int16_t value) {
	sshs_value uValue;
	uValue.setShort(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

int16_t sshsNodeGetShort(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getShort());
}

void sshsNodeCreateInt(sshsNode node, const std::string &key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setInt(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.iintRange = minValue;
	ranges.max.iintRange = maxValue;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutInt(sshsNode node, const std::string &key, int32_t value) {
	sshs_value uValue;
	uValue.setInt(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

int32_t sshsNodeGetInt(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getInt());
}

void sshsNodeCreateLong(sshsNode node, const std::string &key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setLong(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ilongRange = minValue;
	ranges.max.ilongRange = maxValue;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutLong(sshsNode node, const std::string &key, int64_t value) {
	sshs_value uValue;
	uValue.setLong(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

int64_t sshsNodeGetLong(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getLong());
}

void sshsNodeCreateFloat(sshsNode node, const std::string &key, float defaultValue, float minValue, float maxValue,
	int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setFloat(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ffloatRange = minValue;
	ranges.max.ffloatRange = maxValue;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutFloat(sshsNode node, const std::string &key, float value) {
	sshs_value uValue;
	uValue.setFloat(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

float sshsNodeGetFloat(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getFloat());
}

void sshsNodeCreateDouble(sshsNode node, const std::string &key, double defaultValue, double minValue, double maxValue,
	int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setDouble(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ddoubleRange = minValue;
	ranges.max.ddoubleRange = maxValue;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutDouble(sshsNode node, const std::string &key, double value) {
	sshs_value uValue;
	uValue.setDouble(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

double sshsNodeGetDouble(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getDouble());
}

void sshsNodeCreateString(sshsNode node, const std::string &key, const std::string &defaultValue, size_t minLength,
	size_t maxLength, int flags, const std::string &description) {
	sshs_value uValue;
	uValue.setString(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.stringRange = minLength;
	ranges.max.stringRange = maxLength;

	sshsNodeCreateAttribute(node, key, uValue, ranges, flags, description);
}

bool sshsNodePutString(sshsNode node, const std::string &key, const std::string &value) {
	sshs_value uValue;
	uValue.setString(value);

	return (sshsNodePutAttribute(node, key, uValue));
}

// This is a copy of the string on the heap, remember to free() when done!
std::string sshsNodeGetString(sshsNode node, const std::string &key) {
	return (sshsNodeGetAttribute(node, key).getString());
}

void sshsNodeExportNodeToXML(sshsNode node, int outFd) {
	sshsNodeToXML(node, outFd, false);
}

void sshsNodeExportSubTreeToXML(sshsNode node, int outFd) {
	sshsNodeToXML(node, outFd, true);
}

#define INDENT_MAX_LEVEL 20
#define INDENT_SPACES 4
static char spaces[(INDENT_MAX_LEVEL * INDENT_SPACES) + 1] =
	"                                                                                ";

static const char *sshsNodeXMLWhitespaceCallback(mxml_node_t *node, int where) {
	const char *name = mxmlGetElement(node);
	size_t level = 0;

	// Calculate indentation level always.
	for (mxml_node_t *parent = mxmlGetParent(node); parent != nullptr; parent = mxmlGetParent(parent)) {
		level++;
	}

	// Clip indentation level to maximum.
	if (level > INDENT_MAX_LEVEL) {
		level = INDENT_MAX_LEVEL;
	}

	if (strcmp(name, "sshs") == 0) {
		switch (where) {
			case MXML_WS_AFTER_OPEN:
				return ("\n");
				break;

			case MXML_WS_AFTER_CLOSE:
				return ("\n");
				break;

			default:
				break;
		}
	}
	else if (strcmp(name, "node") == 0) {
		switch (where) {
			case MXML_WS_BEFORE_OPEN:
				return (&spaces[((INDENT_MAX_LEVEL - level) * INDENT_SPACES)]);
				break;

			case MXML_WS_AFTER_OPEN:
				return ("\n");
				break;

			case MXML_WS_BEFORE_CLOSE:
				return (&spaces[((INDENT_MAX_LEVEL - level) * INDENT_SPACES)]);
				break;

			case MXML_WS_AFTER_CLOSE:
				return ("\n");
				break;

			default:
				break;
		}
	}
	else if (strcmp(name, "attr") == 0) {
		switch (where) {
			case MXML_WS_BEFORE_OPEN:
				return (&spaces[((INDENT_MAX_LEVEL - level) * INDENT_SPACES)]);
				break;

			case MXML_WS_AFTER_CLOSE:
				return ("\n");
				break;

			default:
				break;
		}
	}

	return (nullptr);
}

static void sshsNodeToXML(sshsNode node, int outFd, bool recursive) {
	mxml_node_t *root = mxmlNewElement(MXML_NO_PARENT, "sshs");
	mxmlElementSetAttr(root, "version", "1.0");
	mxmlAdd(root, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, sshsNodeGenerateXML(node, recursive));

	// Disable wrapping
	mxmlSetWrapMargin(0);

	// Output to file descriptor.
	mxmlSaveFd(root, outFd, &sshsNodeXMLWhitespaceCallback);

	mxmlDelete(root);
}

static mxml_node_t *sshsNodeGenerateXML(sshsNode node, bool recursive) {
	mxml_node_t *thisNode = mxmlNewElement(MXML_NO_PARENT, "node");

	// First this node's name and full path.
	mxmlElementSetAttr(thisNode, "name", sshsNodeGetName(node));
	mxmlElementSetAttr(thisNode, "path", sshsNodeGetPath(node));

	{
		std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

		// Then it's attributes (key:value pairs).
		for (const auto &attr : node->attributes) {
			// If an attribute is marked NO_EXPORT, we skip it.
			if ((attr.second.flags & SSHS_FLAGS_NO_EXPORT)) {
				continue;
			}

			const std::string type = sshsHelperTypeToStringConverter(attr.second.value.getType());
			const std::string value = sshsHelperValueToStringConverter(attr.second.value);

			mxml_node_t *xmlAttr = mxmlNewElement(thisNode, "attr");
			mxmlElementSetAttr(xmlAttr, "key", attr.first.c_str());
			mxmlElementSetAttr(xmlAttr, "type", type.c_str());
			mxmlNewText(xmlAttr, 0, value.c_str());
		}
	}

	// And lastly recurse down to the children.
	if (recursive) {
		size_t numChildren;
		sshsNode *children = sshsNodeGetChildren(node, &numChildren);

		for (size_t i = 0; i < numChildren; i++) {
			mxml_node_t *child = sshsNodeGenerateXML(children[i], recursive);

			if (mxmlGetFirstChild(child) != nullptr) {
				mxmlAdd(thisNode, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, child);
			}
			else {
				// Free memory if not adding.
				mxmlDelete(child);
			}
		}

		free(children);
	}

	return (thisNode);
}

bool sshsNodeImportNodeFromXML(sshsNode node, int inFd, bool strict) {
	return (sshsNodeFromXML(node, inFd, false, strict));
}

bool sshsNodeImportSubTreeFromXML(sshsNode node, int inFd, bool strict) {
	return (sshsNodeFromXML(node, inFd, true, strict));
}

static mxml_node_t **sshsNodeXMLFilterChildNodes(mxml_node_t *node, const char *nodeName, size_t *numChildren) {
	// Go through once to count the number of matching children.
	size_t matchedChildren = 0;

	for (mxml_node_t *current = mxmlGetFirstChild(node); current != nullptr; current = mxmlGetNextSibling(current)) {
		const char *name = mxmlGetElement(current);

		if (name != nullptr && strcmp(name, nodeName) == 0) {
			matchedChildren++;
		}
	}

	// If none, exit gracefully.
	if (matchedChildren == 0) {
		*numChildren = 0;
		return (nullptr);
	}

	// Now allocate appropriate memory for list.
	mxml_node_t **filteredNodes = (mxml_node_t **) malloc(matchedChildren * sizeof(mxml_node_t *));
	SSHS_MALLOC_CHECK_EXIT(filteredNodes);

	// Go thorough again and collect the matching nodes.
	size_t i = 0;
	for (mxml_node_t *current = mxmlGetFirstChild(node); current != nullptr; current = mxmlGetNextSibling(current)) {
		const char *name = mxmlGetElement(current);

		if (name != nullptr && strcmp(name, nodeName) == 0) {
			filteredNodes[i++] = current;
		}
	}

	*numChildren = matchedChildren;
	return (filteredNodes);
}

static bool sshsNodeFromXML(sshsNode node, int inFd, bool recursive, bool strict) {
	mxml_node_t *root = mxmlLoadFd(nullptr, inFd, MXML_OPAQUE_CALLBACK);

	if (root == nullptr) {
		(*sshsGetGlobalErrorLogCallback())("Failed to load XML from file descriptor.");
		return (false);
	}

	// Check name and version for compliance.
	if ((strcmp(mxmlGetElement(root), "sshs") != 0) || (strcmp(mxmlElementGetAttr(root, "version"), "1.0") != 0)) {
		mxmlDelete(root);
		(*sshsGetGlobalErrorLogCallback())("Invalid SSHS v1.0 XML content.");
		return (false);
	}

	size_t numChildren = 0;
	mxml_node_t **children = sshsNodeXMLFilterChildNodes(root, "node", &numChildren);

	if (numChildren != 1) {
		mxmlDelete(root);
		free(children);
		(*sshsGetGlobalErrorLogCallback())("Multiple or no root child nodes present.");
		return (false);
	}

	mxml_node_t *rootNode = children[0];

	free(children);

	// Strict mode: check if names match.
	if (strict) {
		const char *rootNodeName = mxmlElementGetAttr(rootNode, "name");

		if (rootNodeName == nullptr || strcmp(rootNodeName, sshsNodeGetName(node)) != 0) {
			mxmlDelete(root);
			(*sshsGetGlobalErrorLogCallback())("Names don't match (required in 'strict' mode).");
			return (false);
		}
	}

	sshsNodeConsumeXML(node, rootNode, recursive);

	mxmlDelete(root);

	return (true);
}

static void sshsNodeConsumeXML(sshsNode node, mxml_node_t *content, bool recursive) {
	size_t numAttrChildren = 0;
	mxml_node_t **attrChildren = sshsNodeXMLFilterChildNodes(content, "attr", &numAttrChildren);

	for (size_t i = 0; i < numAttrChildren; i++) {
		// Check that the proper attributes exist.
		const char *key = mxmlElementGetAttr(attrChildren[i], "key");
		const char *type = mxmlElementGetAttr(attrChildren[i], "type");

		if (key == nullptr || type == nullptr) {
			continue;
		}

		// Get the needed values.
		const char *value = mxmlGetOpaque(attrChildren[i]);

		if (!sshsNodeStringToAttributeConverter(node, key, type, value)) {
			// Ignore read-only/range errors.
			if (errno == EPERM || errno == ERANGE) {
				continue;
			}

			char errorMsg[1024];
			snprintf(errorMsg, 1024, "Failed to convert attribute '%s' of type '%s' with value '%s' from XML.", key,
				type, value);

			(*sshsGetGlobalErrorLogCallback())(errorMsg);
		}
	}

	free(attrChildren);

	if (recursive) {
		size_t numNodeChildren = 0;
		mxml_node_t **nodeChildren = sshsNodeXMLFilterChildNodes(content, "node", &numNodeChildren);

		for (size_t i = 0; i < numNodeChildren; i++) {
			// Check that the proper attributes exist.
			const char *childName = mxmlElementGetAttr(nodeChildren[i], "name");

			if (childName == nullptr) {
				continue;
			}

			// Get the child node.
			sshsNode childNode = sshsNodeGetChild(node, childName);

			// If not existing, try to create.
			if (childNode == nullptr) {
				childNode = sshsNodeAddChild(node, childName);
			}

			// And call recursively.
			sshsNodeConsumeXML(childNode, nodeChildren[i], recursive);
		}

		free(nodeChildren);
	}
}

// For more precise failure reason, look at errno.
bool sshsNodeStringToAttributeConverter(sshsNode node, const char *key, const char *typeStr, const char *valueStr) {
	// Parse the values according to type and put them in the node.
	enum sshs_node_attr_value_type type;
	type = sshsHelperStringToTypeConverter(typeStr);

	if (type == SSHS_UNKNOWN) {
		errno = EINVAL;
		return (false);
	}

	sshs_value value;
	value = sshsHelperStringToValueConverter(type, valueStr);

	// IFF attribute already exists, we update it using sshsNodePut(), else
	// we create the attribute with maximum range and a default description.
	// These XMl-loaded attributes are also marked NO_EXPORT.
	// This happens on XML load only. More restrictive ranges and flags can be
	// enabled later by calling sshsNodeCreate*() again as needed.
	bool result = false;

	if (sshsNodeAttributeExists(node, key, type)) {
		result = sshsNodePutAttribute(node, key, value);
	}
	else {
		// Create never fails!
		result = true;

		switch (type) {
			case SSHS_BOOL:
				sshsNodeCreateBool(node, key, value.getBool(), SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_BYTE:
				sshsNodeCreateByte(node, key, value.getByte(), INT8_MIN, INT8_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_SHORT:
				sshsNodeCreateShort(node, key, value.getShort(), INT16_MIN, INT16_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_INT:
				sshsNodeCreateInt(node, key, value.getInt(), INT32_MIN, INT32_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_LONG:
				sshsNodeCreateLong(node, key, value.getLong(), INT64_MIN, INT64_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_FLOAT:
				sshsNodeCreateFloat(node, key, value.getFloat(), -FLT_MAX, FLT_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_DOUBLE:
				sshsNodeCreateDouble(node, key, value.getDouble(), -DBL_MAX, DBL_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_STRING:
				sshsNodeCreateString(node, key, value.getString(), 0, INT32_MAX,
					SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT, "XML loaded value.");
				break;

			case SSHS_UNKNOWN:
				errno = EINVAL;
				result = false;
				break;
		}
	}

	return (result);
}

const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames) {
	size_t numChildren;
	sshsNode *children = sshsNodeGetChildren(node, &numChildren);

	if (children == nullptr) {
		*numNames = 0;
		errno = ENOENT;
		return (nullptr);
	}

	const char **childNames = (const char **) malloc(numChildren * sizeof(*childNames));
	SSHS_MALLOC_CHECK_EXIT(childNames);

	// Copy pointers to name string over. Safe because nodes are never deleted.
	for (size_t i = 0; i < numChildren; i++) {
		childNames[i] = sshsNodeGetName(children[i]);
	}

	free(children);

	*numNames = numChildren;
	return (childNames);
}

const char **sshsNodeGetAttributeKeys(sshsNode node, size_t *numKeys) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (node->attributes.empty()) {
		*numKeys = 0;
		errno = ENOENT;
		return (nullptr);
	}

	size_t numAttributes = node->attributes.size();

	const char **attributeKeys = (const char **) malloc(numAttributes * sizeof(*attributeKeys));
	SSHS_MALLOC_CHECK_EXIT(attributeKeys);

	// Copy pointers to key string over. Safe because attributes are never deleted.
	size_t i = 0;
	for (const auto &attr : node->attributes) {
		attributeKeys[i++] = attr.first.c_str();
	}

	*numKeys = numAttributes;
	return (attributeKeys);
}

enum sshs_node_attr_value_type *sshsNodeGetAttributeTypes(sshsNode node, const std::string &key, size_t *numTypes) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributes.count(key)) {
		*numTypes = 0;
		errno = ENOENT;
		return (nullptr);
	}

	// There is at most 1 type for one specific attribute key.
	enum sshs_node_attr_value_type *attributeTypes = (enum sshs_node_attr_value_type *) malloc(sizeof(*attributeTypes));
	SSHS_MALLOC_CHECK_EXIT(attributeTypes);

	// Check each attribute if it matches, and save its type if true.
	attributeTypes[0] = node->attributes[key].value.getType();

	*numTypes = 1;
	return (attributeTypes);
}

struct sshs_node_attr_ranges sshsNodeGetAttributeRanges(sshsNode node, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeRanges", key, SSHS_UNKNOWN);
	}

	sshs_node_attr &attr = node->attributes[key];

	struct sshs_node_attr_ranges result;
	result.min = attr.min;
	result.max = attr.max;

	return (result);
}

int sshsNodeGetAttributeFlags(sshsNode node, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeFlags", key, SSHS_UNKNOWN);
	}

	sshs_node_attr &attr = node->attributes[key];

	return (attr.flags);
}

char *sshsNodeGetAttributeDescription(sshsNode node, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeDescription", key, SSHS_UNKNOWN);
	}

	sshs_node_attr &attr = node->attributes[key];

	char *descriptionCopy = strdup(attr.description.c_str());
	SSHS_MALLOC_CHECK_EXIT(descriptionCopy);

	return (descriptionCopy);
}
