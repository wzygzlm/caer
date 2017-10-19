#include "sshs_internal.hpp"

#include <cfloat>
#include <memory>
#include <algorithm>
#include <vector>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

// We don't care about unlocking anything here, as we exit hard on error anyway.
static inline void sshsNodeError(const std::string &funcName, const std::string &key,
	enum sshs_node_attr_value_type type, const std::string &msg, bool fatal = true) {
	boost::format errorMsg = boost::format("%s(): attribute '%s' (type '%s'): %s.") % funcName % key
		% sshsHelperCppTypeToStringConverter(type) % msg;

	(*sshsGetGlobalErrorLogCallback())(errorMsg.str().c_str());

	if (fatal) {
		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}
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

	bool isFlagSet(int flag) const noexcept {
		return ((flags & flag) == flag);
	}
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
		return (!this->operator==(rhs));
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
		return (!this->operator==(rhs));
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

	sshs_node(const std::string &_name, sshsNode _parent) :
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

	void createAttribute(const std::string &key, const sshs_value &defaultValue,
		const struct sshs_node_attr_ranges &ranges, int flags, const std::string &description) {
		// Parse range struct.
		union sshs_node_attr_range minValue = ranges.min;
		union sshs_node_attr_range maxValue = ranges.max;

		// Strings are special, their length range goes from 0 to SIZE_MAX, but we
		// have to restrict that to from 0 to INT32_MAX for languages like Java
		// that only support integer string lengths. It's also reasonable.
		if (defaultValue.getType() == SSHS_STRING) {
			if ((minValue.stringRange > INT32_MAX) || (maxValue.stringRange > INT32_MAX)) {
				boost::format errorMsg = boost::format("minimum/maximum string range value outside allowed limits. "
					"Please make sure the value is positive, between 0 and %d!") % INT32_MAX;

				sshsNodeError("sshsNodeCreateAttribute", key, SSHS_STRING, errorMsg.str());
			}
		}

		// Check that value conforms to range limits.
		if (!defaultValue.inRange(minValue, maxValue)) {
			// Fail on wrong default value. Must be within range!
			boost::format errorMsg = boost::format("default value '%s' is out of specified range. "
				"Please make sure the default value is within the given range!")
				% sshsHelperCppValueToStringConverter(defaultValue);

			sshsNodeError("sshsNodeCreateAttribute", key, defaultValue.getType(), errorMsg.str());
		}

		// Restrict NOTIFY_ONLY flag to booleans only, for button-like behavior.
		if ((flags & SSHS_FLAGS_NOTIFY_ONLY) && defaultValue.getType() != SSHS_BOOL) {
			// Fail on wrong notify-only flag usage.
			sshsNodeError("sshsNodeCreateAttribute", key, defaultValue.getType(),
				"the NOTIFY_ONLY flag is set, but attribute is not of type BOOL. Only booleans can have this flag set!");
		}

		sshs_node_attr newAttr;

		newAttr.value = defaultValue;

		newAttr.min = minValue;
		newAttr.max = maxValue;
		newAttr.flags = flags;
		newAttr.description = description;

		std::lock_guard<std::recursive_mutex> lock(node_lock);

		// Add if not present. Else update value (below).
		if (!attributes.count(key)) {
			attributes[key] = newAttr;

			// Listener support. Call only on change, which is always the case here.
			for (const auto &l : attrListeners) {
				(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_ADDED, key.c_str(), newAttr.value.getType(),
					newAttr.value.toCUnion(true));
			}
		}
		else {
			const sshs_value &oldAttrValue = attributes[key].value;

			// To simplify things, we don't support multiple types per key (though the API does).
			if (oldAttrValue.getType() != newAttr.value.getType()) {
				boost::format errorMsg = boost::format(
					"value with this key already exists and has a different type of '%s'")
					% sshsHelperCppTypeToStringConverter(oldAttrValue.getType());

				sshsNodeError("sshsNodeCreateAttribute", key, newAttr.value.getType(), errorMsg.str());
			}

			// Check if the current value is still fine and within range; if it is
			// we use it, else just use the new value.
			if (oldAttrValue.inRange(minValue, maxValue)) {
				// Only update value, then use newAttr. No listeners called since this
				// is by definition the old value and as such nothing can have changed.
				newAttr.value = oldAttrValue;
				attributes[key] = newAttr;
			}
			else {
				// If the old value is not in range anymore, the new value must be different,
				// since it is guaranteed to be inside the new range. So we call the listeners.
				attributes[key] = newAttr;

				// Listener support. Call only on change, which is always the case here.
				for (const auto &l : attrListeners) {
					(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_MODIFIED, key.c_str(),
						newAttr.value.getType(), newAttr.value.toCUnion(true));
				}
			}
		}
	}

	void removeAttribute(const std::string &key, enum sshs_node_attr_value_type type) {
		std::lock_guard<std::recursive_mutex> lock(node_lock);

		if (!attributeExists(key, type)) {
			// Ignore calls on non-existent attributes for remove, as it is used
			// to clean-up attributes before re-creating them in a consistent way.
			return;
		}

		sshs_node_attr &attr = attributes[key];

		// Listener support.
		for (const auto &l : attrListeners) {
			(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_REMOVED, key.c_str(), attr.value.getType(),
				attr.value.toCUnion(true));
		}

		// Remove attribute from node.
		attributes.erase(key);
	}

	void removeAllAttributes() {
		std::lock_guard<std::recursive_mutex> lock(node_lock);

		for (const auto &attr : attributes) {
			for (const auto &l : attrListeners) {
				(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_REMOVED, attr.first.c_str(),
					attr.second.value.getType(), attr.second.value.toCUnion(true));
			}
		}

		attributes.clear();
	}

	bool attributeExists(const std::string &key, enum sshs_node_attr_value_type type) {
		std::lock_guard<std::recursive_mutex> lockNode(node_lock);

		if ((!attributes.count(key)) || (attributes[key].value.getType() != type)) {
			errno = ENOENT;
			return (false);
		}

		// The specified attribute exists and has a matching type.
		return (true);
	}

	const sshs_value getAttribute(const std::string &key, enum sshs_node_attr_value_type type) {
		std::lock_guard<std::recursive_mutex> lockNode(node_lock);

		if (!attributeExists(key, type)) {
			sshsNodeErrorNoAttribute("sshsNodeGetAttribute", key, type);
		}

		// Return a copy of the final value.
		return (attributes[key].value);
	}

	bool putAttribute(const std::string &key, const sshs_value &value, bool forceReadOnlyUpdate = false) {
		std::lock_guard<std::recursive_mutex> lockNode(node_lock);

		if (!attributeExists(key, value.getType())) {
			sshsNodeErrorNoAttribute("sshsNodePutAttribute", key, value.getType());
		}

		sshs_node_attr &attr = attributes[key];

		// Value must be present, so update old one, after checking range and flags.
		if ((!forceReadOnlyUpdate && attr.isFlagSet(SSHS_FLAGS_READ_ONLY))
			|| (forceReadOnlyUpdate && !attr.isFlagSet(SSHS_FLAGS_READ_ONLY))) {
			// Read-only flag set, cannot put new value!
			errno = EPERM;
			return (false);
		}

		if (!value.inRange(attr.min, attr.max)) {
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
			for (const auto &l : attrListeners) {
				(*l.getListener())(this, l.getUserData(), SSHS_ATTRIBUTE_MODIFIED, key.c_str(), attr.value.getType(),
					attr.value.toCUnion(true));
			}
		}

		return (true);
	}
};

static void sshsNodeDestroy(sshsNode node);
static void sshsNodeRemoveSubTree(sshsNode node);
static void sshsNodeRemoveChild(sshsNode node, const char *childName);
static void sshsNodeRemoveAllChildren(sshsNode node);

#define XML_INDENT_SPACES 4

static bool sshsNodeToXML(sshsNode node, const std::string &fileName, bool recursive);
static void sshsNodeGenerateXML(sshsNode node, boost::property_tree::ptree &content, bool recursive);
static bool sshsNodeFromXML(sshsNode node, const std::string &fileName, bool recursive, bool strict);
static void sshsNodeConsumeXML(sshsNode node, const boost::property_tree::ptree &content, bool recursive);

sshsNode sshsNodeNew(const char *nodeName, sshsNode parent) {
	sshsNode newNode = new sshs_node(nodeName, parent);
	sshsMemoryCheck(newNode, __func__);

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

// This returns a reference to a node, and as such must be carefully mediated with
// any sshsNodeRemoveNode() calls.
sshsNode sshsNodeGetChild(sshsNode node, const char* childName) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	if (node->children.count(childName)) {
		return (node->children[childName]);
	}
	else {
		return (nullptr);
	}
}

// Remember to free the resulting array. This returns references to nodes,
// and as such must be carefully mediated with any sshsNodeRemoveNode() calls.
sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	size_t childrenCount = node->children.size();

	// If none, exit gracefully.
	if (childrenCount == 0) {
		*numChildren = 0;
		return (nullptr);
	}

	sshsNode *children = (sshsNode *) malloc(childrenCount * sizeof(*children));
	sshsMemoryCheck(children, __func__);

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

void sshsNodeClearSubTree(sshsNode startNode, bool clearStartNode) {
	std::lock_guard<std::recursive_mutex> lockNode(startNode->node_lock);

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

// Eliminates this node and any children. Nobody can have a reference, or
// be in the process of getting one, to this node or any of its children.
// You need to make sure of this in your application!
void sshsNodeRemoveNode(sshsNode node) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

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
		// Verify that a valid node exists, else simply return
		// without doing anything. Node was already deleted.
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
			(*l.getListener())(node, l.getUserData(), SSHS_CHILD_NODE_REMOVED, child.first.c_str());
		}

		sshsNodeDestroy(child.second);
	}

	node->children.clear();
}

void sshsNodeCreateAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value defaultValue, const struct sshs_node_attr_ranges ranges, int flags,
	const char *description) {
	sshs_value val;
	val.fromCUnion(defaultValue, type);

	node->createAttribute(key, val, ranges, flags, description);
}

void sshsNodeRemoveAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	node->removeAttribute(key, type);
}

void sshsNodeRemoveAllAttributes(sshsNode node) {
	node->removeAllAttributes();
}

bool sshsNodeAttributeExists(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	return (node->attributeExists(key, type));
}

bool sshsNodePutAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value value) {
	sshs_value val;
	val.fromCUnion(value, type);

	return (node->putAttribute(key, val));
}

union sshs_node_attr_value sshsNodeGetAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	return (node->getAttribute(key, type).toCUnion());
}

bool sshsNodeUpdateReadOnlyAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value value) {
	sshs_value val;
	val.fromCUnion(value, type);

	return (node->putAttribute(key, val, true));
}

void sshsNodeCreateBool(sshsNode node, const char *key, bool defaultValue, int flags, const char *description) {
	sshs_value uValue;
	uValue.setBool(defaultValue);

	// No range for booleans.
	struct sshs_node_attr_ranges ranges;
	ranges.min.ilongRange = 0;
	ranges.max.ilongRange = 0;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutBool(sshsNode node, const char *key, bool value) {
	sshs_value uValue;
	uValue.setBool(value);

	return (node->putAttribute(key, uValue));
}

bool sshsNodeGetBool(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_BOOL).getBool());
}

void sshsNodeCreateByte(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setByte(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ibyteRange = minValue;
	ranges.max.ibyteRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutByte(sshsNode node, const char *key, int8_t value) {
	sshs_value uValue;
	uValue.setByte(value);

	return (node->putAttribute(key, uValue));
}

int8_t sshsNodeGetByte(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_BYTE).getByte());
}

void sshsNodeCreateShort(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setShort(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ishortRange = minValue;
	ranges.max.ishortRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutShort(sshsNode node, const char *key, int16_t value) {
	sshs_value uValue;
	uValue.setShort(value);

	return (node->putAttribute(key, uValue));
}

int16_t sshsNodeGetShort(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_SHORT).getShort());
}

void sshsNodeCreateInt(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setInt(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.iintRange = minValue;
	ranges.max.iintRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutInt(sshsNode node, const char *key, int32_t value) {
	sshs_value uValue;
	uValue.setInt(value);

	return (node->putAttribute(key, uValue));
}

int32_t sshsNodeGetInt(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_INT).getInt());
}

void sshsNodeCreateLong(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setLong(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ilongRange = minValue;
	ranges.max.ilongRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutLong(sshsNode node, const char *key, int64_t value) {
	sshs_value uValue;
	uValue.setLong(value);

	return (node->putAttribute(key, uValue));
}

int64_t sshsNodeGetLong(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_LONG).getLong());
}

void sshsNodeCreateFloat(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue, int flags,
	const char *description) {
	sshs_value uValue;
	uValue.setFloat(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ffloatRange = minValue;
	ranges.max.ffloatRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutFloat(sshsNode node, const char *key, float value) {
	sshs_value uValue;
	uValue.setFloat(value);

	return (node->putAttribute(key, uValue));
}

float sshsNodeGetFloat(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_FLOAT).getFloat());
}

void sshsNodeCreateDouble(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setDouble(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.ddoubleRange = minValue;
	ranges.max.ddoubleRange = maxValue;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutDouble(sshsNode node, const char *key, double value) {
	sshs_value uValue;
	uValue.setDouble(value);

	return (node->putAttribute(key, uValue));
}

double sshsNodeGetDouble(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_DOUBLE).getDouble());
}

void sshsNodeCreateString(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	int flags, const char *description) {
	sshs_value uValue;
	uValue.setString(defaultValue);

	struct sshs_node_attr_ranges ranges;
	ranges.min.stringRange = minLength;
	ranges.max.stringRange = maxLength;

	node->createAttribute(key, uValue, ranges, flags, description);
}

bool sshsNodePutString(sshsNode node, const char *key, const char *value) {
	sshs_value uValue;
	uValue.setString(value);

	return (node->putAttribute(key, uValue));
}

// This is a copy of the string on the heap, remember to free() when done!
char *sshsNodeGetString(sshsNode node, const char *key) {
	return (node->getAttribute(key, SSHS_STRING).toCUnion().string);
}

bool sshsNodeExportNodeToXML(sshsNode node, const char *fileName) {
	return (sshsNodeToXML(node, fileName, false));
}

bool sshsNodeExportSubTreeToXML(sshsNode node, const char *fileName) {
	return (sshsNodeToXML(node, fileName, true));
}

static bool sshsNodeToXML(sshsNode node, const std::string &fileName, bool recursive) {
	boost::property_tree::ptree xmlTree;

	std::ofstream outStream(fileName, std::ios::trunc);
	if (!outStream.is_open()) {
		(*sshsGetGlobalErrorLogCallback())("Failed to open file for writing.");
		return (false);
	}

	// Add main SSHS node and version.
	xmlTree.put("sshs.<xmlattr>.version", "1.0");

	// Generate recursive XML for all nodes.
	sshsNodeGenerateXML(node, xmlTree.get_child("sshs.node"), recursive);

	try {
		boost::property_tree::xml_parser::xml_writer_settings<std::string> xmlIndent(' ', XML_INDENT_SPACES);
		boost::property_tree::write_xml(outStream, xmlTree, xmlIndent);
	}
	catch (const boost::property_tree::xml_parser_error &ex) {
		const std::string errorMsg = std::string("Failed to write XML to output stream. Exception: ") + ex.what();
		(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str());
		return (false);
	}

	return (true);
}

static void sshsNodeGenerateXML(sshsNode node, boost::property_tree::ptree &content, bool recursive) {
	content.put("<xmlattr>.name", node->name);
	content.put("<xmlattr>.path", node->path);

//	{
//		std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);
//
//		// Then it's attributes (key:value pairs).
//		for (const auto &attr : node->attributes) {
//			// If an attribute is marked NO_EXPORT, we skip it.
//			if (attr.second.isFlagSet(SSHS_FLAGS_NO_EXPORT)) {
//				continue;
//			}
//
//			const std::string type = sshsHelperCppTypeToStringConverter(attr.second.value.getType());
//			const std::string value = sshsHelperCppValueToStringConverter(attr.second.value);
//
//			mxml_node_t *xmlAttr = mxmlNewElement(thisNode, "attr");
//			mxmlElementSetAttr(xmlAttr, "key", attr.first.c_str());
//			mxmlElementSetAttr(xmlAttr, "type", type.c_str());
//			mxmlNewText(xmlAttr, 0, value.c_str());
//		}
//	}

	// And lastly recurse down to the children.
	if (recursive) {
		std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

		for (const auto &child : node->children) {
			sshsNodeGenerateXML(child.second, content, recursive);

			// TODO: handle nodes with no children.
		}
	}
}

bool sshsNodeImportNodeFromXML(sshsNode node, const char *fileName, bool strict) {
	return (sshsNodeFromXML(node, fileName, false, strict));
}

bool sshsNodeImportSubTreeFromXML(sshsNode node, const char *fileName, bool strict) {
	return (sshsNodeFromXML(node, fileName, true, strict));
}

static std::vector<std::reference_wrapper<const boost::property_tree::ptree>> sshsNodeXMLFilterChildNodes(
	const boost::property_tree::ptree &content, const std::string &name) {
	std::vector<std::reference_wrapper<const boost::property_tree::ptree>> result;

	for (const auto &elem : content) {
		if (elem.first == name) {
			result.push_back(elem.second);
		}
	}

	return (result);
}

static bool sshsNodeFromXML(sshsNode node, const std::string &fileName, bool recursive, bool strict) {
	boost::property_tree::ptree xmlTree;

	std::ifstream inStream(fileName);
	if (!inStream.is_open()) {
		(*sshsGetGlobalErrorLogCallback())("Failed to open file for reading.");
		return (false);
	}

	try {
		boost::property_tree::read_xml(inStream, xmlTree, boost::property_tree::xml_parser::trim_whitespace);
	}
	catch (const boost::property_tree::xml_parser_error &ex) {
		const std::string errorMsg = std::string("Failed to load XML from input stream. Exception: ") + ex.what();
		(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str());
		return (false);
	}

	// Check name and version for compliance.
	try {
		const auto sshsVersion = xmlTree.get<std::string>("sshs.<xmlattr>.version");
		if (sshsVersion != "1.0") {
			throw boost::property_tree::ptree_error("unsupported SSHS version (supported: '1.0').");
		}
	}
	catch (const boost::property_tree::ptree_error &ex) {
		const std::string errorMsg = std::string("Invalid XML content. Exception: ") + ex.what();
		(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str());
		return (false);
	}

	auto root = sshsNodeXMLFilterChildNodes(xmlTree.get_child("sshs"), "node");

	if (root.size() != 1) {
		(*sshsGetGlobalErrorLogCallback())("Multiple or no root child nodes present.");
		return (false);
	}

	auto &rootNode = root.front().get();

	// Strict mode: check if names match.
	if (strict) {
		try {
			const auto rootNodeName = rootNode.get<std::string>("<xmlattr>.name");

			if (rootNodeName != node->name) {
				throw boost::property_tree::ptree_error("names don't match (required in 'strict' mode).");
			}
		}
		catch (const boost::property_tree::ptree_error &ex) {
			const std::string errorMsg = std::string("Invalid root node. Exception: ") + ex.what();
			(*sshsGetGlobalErrorLogCallback())(errorMsg.c_str());
			return (false);
		}
	}

	sshsNodeConsumeXML(node, rootNode, recursive);

	return (true);
}

static void sshsNodeConsumeXML(sshsNode node, const boost::property_tree::ptree &content, bool recursive) {
	auto attributes = sshsNodeXMLFilterChildNodes(content, "attr");

	for (auto &attr : attributes) {
		// Check that the proper attributes exist.
		const auto key = attr.get().get("<xmlattr>.key", "");
		const auto type = attr.get().get("<xmlattr>.type", "");

		if (key.empty() || type.empty()) {
			continue;
		}

		// Get the needed values.
		const auto value = attr.get().get_value("");

		if (!sshsNodeStringToAttributeConverter(node, key.c_str(), type.c_str(), value.c_str())) {
			// Ignore read-only/range errors.
			if (errno == EPERM || errno == ERANGE) {
				continue;
			}

			boost::format errorMsg = boost::format("failed to convert attribute from XML, value string was '%s'")
				% value;

			sshsNodeError("sshsNodeConsumeXML", key, sshsHelperCppStringToTypeConverter(type), errorMsg.str(), false);
		}
	}

	if (recursive) {
		auto children = sshsNodeXMLFilterChildNodes(content, "node");

		for (auto &child : children) {
			// Check that the proper attributes exist.
			const auto childName = child.get().get("<xmlattr>.name", "");

			if (childName.empty()) {
				continue;
			}

			// Get the child node.
			sshsNode childNode = sshsNodeGetChild(node, childName.c_str());

			// If not existing, try to create.
			if (childNode == nullptr) {
				childNode = sshsNodeAddChild(node, childName.c_str());
			}

			// And call recursively.
			sshsNodeConsumeXML(childNode, child.get(), recursive);
		}
	}
}

// For more precise failure reason, look at errno.
bool sshsNodeStringToAttributeConverter(sshsNode node, const char *key, const char *typeStr, const char *valueStr) {
	// Parse the values according to type and put them in the node.
	enum sshs_node_attr_value_type type;
	type = sshsHelperCppStringToTypeConverter(typeStr);

	if (type == SSHS_UNKNOWN) {
		errno = EINVAL;
		return (false);
	}

	if ((type == SSHS_STRING) && (valueStr == nullptr)) {
		// Empty string.
		valueStr = "";
	}

	sshs_value value;
	try {
		value = sshsHelperCppStringToValueConverter(type, valueStr);
	}
	catch (const std::invalid_argument &) {
		errno = EINVAL;
		return (false);
	}
	catch (const std::out_of_range &) {
		errno = EINVAL;
		return (false);
	}

	// IFF attribute already exists, we update it using sshsNodePut(), else
	// we create the attribute with maximum range and a default description.
	// These XMl-loaded attributes are also marked NO_EXPORT.
	// This happens on XML load only. More restrictive ranges and flags can be
	// enabled later by calling sshsNodeCreate*() again as needed.
	bool result = false;

	if (node->attributeExists(key, type)) {
		result = node->putAttribute(key, value);
	}
	else {
		// Create never fails, it may exit the program, but not fail!
		result = true;
		struct sshs_node_attr_ranges ranges;

		switch (type) {
			case SSHS_BOOL:
				ranges.min.ilongRange = 0;
				ranges.max.ilongRange = 0;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_BYTE:
				ranges.min.ibyteRange = INT8_MIN;
				ranges.max.ibyteRange = INT8_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_SHORT:
				ranges.min.ishortRange = INT16_MIN;
				ranges.max.ishortRange = INT16_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_INT:
				ranges.min.iintRange = INT32_MIN;
				ranges.max.iintRange = INT32_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_LONG:
				ranges.min.ilongRange = INT64_MIN;
				ranges.max.ilongRange = INT64_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_FLOAT:
				ranges.min.ffloatRange = -FLT_MAX;
				ranges.max.ffloatRange = FLT_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_DOUBLE:
				ranges.min.ddoubleRange = -DBL_MAX;
				ranges.max.ddoubleRange = DBL_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_STRING:
				ranges.min.stringRange = 0;
				ranges.max.stringRange = INT32_MAX;
				node->createAttribute(key, value, ranges, SSHS_FLAGS_NORMAL | SSHS_FLAGS_NO_EXPORT,
					"XML loaded value.");
				break;

			case SSHS_UNKNOWN:
				errno = EINVAL;
				result = false;
				break;
		}
	}

	return (result);
}

// Remember to free the resulting array.
const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames) {
	std::shared_lock<std::shared_timed_mutex> lock(node->traversal_lock);

	if (node->children.empty()) {
		*numNames = 0;
		errno = ENOENT;
		return (nullptr);
	}

	size_t numChildren = node->children.size();

	// Nodes can be deleted, so we copy the string's contents into
	// memory that will be guaranteed to exist.
	size_t childNamesLength = 0;

	for (const auto &child : node->children) {
		// Length plus one for terminating NUL byte.
		childNamesLength += child.first.length() + 1;
	}

	char **childNames = (char **) malloc((numChildren * sizeof(char *)) + childNamesLength);
	sshsMemoryCheck(childNames, __func__);

	size_t offset = (numChildren * sizeof(char *));

	size_t i = 0;
	for (const auto &child : node->children) {
		// We have all the memory, so now copy the strings over and set the
		// pointers as if an array of pointers was the only result.
		childNames[i] = (char *) (((uint8_t *) childNames) + offset);
		strcpy(childNames[i], child.first.c_str());

		// Length plus one for terminating NUL byte.
		offset += child.first.length() + 1;
		i++;
	}

	*numNames = numChildren;
	return (const_cast<const char **>(childNames));
}

// Remember to free the resulting array.
const char **sshsNodeGetAttributeKeys(sshsNode node, size_t *numKeys) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (node->attributes.empty()) {
		*numKeys = 0;
		errno = ENOENT;
		return (nullptr);
	}

	size_t numAttributes = node->attributes.size();

	// Attributes can be deleted, so we copy the key string's contents into
	// memory that will be guaranteed to exist.
	size_t attributeKeysLength = 0;

	for (const auto &attr : node->attributes) {
		// Length plus one for terminating NUL byte.
		attributeKeysLength += attr.first.length() + 1;
	}

	char **attributeKeys = (char **) malloc((numAttributes * sizeof(char *)) + attributeKeysLength);
	sshsMemoryCheck(attributeKeys, __func__);

	size_t offset = (numAttributes * sizeof(char *));

	size_t i = 0;
	for (const auto &attr : node->attributes) {
		// We have all the memory, so now copy the strings over and set the
		// pointers as if an array of pointers was the only result.
		attributeKeys[i] = (char *) (((uint8_t *) attributeKeys) + offset);
		strcpy(attributeKeys[i], attr.first.c_str());

		// Length plus one for terminating NUL byte.
		offset += attr.first.length() + 1;
		i++;
	}

	*numKeys = numAttributes;
	return (const_cast<const char **>(attributeKeys));
}

// Remember to free the resulting array.
enum sshs_node_attr_value_type *sshsNodeGetAttributeTypes(sshsNode node, const char *key, size_t *numTypes) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!node->attributes.count(key)) {
		*numTypes = 0;
		errno = ENOENT;
		return (nullptr);
	}

	// There is at most 1 type for one specific attribute key.
	enum sshs_node_attr_value_type *attributeTypes = (enum sshs_node_attr_value_type *) malloc(
		1 * sizeof(*attributeTypes));
	sshsMemoryCheck(attributeTypes, __func__);

	// Check each attribute if it matches, and save its type if true.
	// We only support one type per attribute key here.
	attributeTypes[0] = node->attributes[key].value.getType();

	*numTypes = 1;
	return (attributeTypes);
}

struct sshs_node_attr_ranges sshsNodeGetAttributeRanges(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeRanges", key, type);
	}

	sshs_node_attr &attr = node->attributes[key];

	struct sshs_node_attr_ranges result;
	result.min = attr.min;
	result.max = attr.max;

	return (result);
}

int sshsNodeGetAttributeFlags(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeFlags", key, type);
	}

	sshs_node_attr &attr = node->attributes[key];

	return (attr.flags);
}

// Remember to free the resulting string.
char *sshsNodeGetAttributeDescription(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	std::lock_guard<std::recursive_mutex> lockNode(node->node_lock);

	if (!sshsNodeAttributeExists(node, key, type)) {
		sshsNodeErrorNoAttribute("sshsNodeGetAttributeDescription", key, type);
	}

	sshs_node_attr &attr = node->attributes[key];

	char *descriptionCopy = strdup(attr.description.c_str());
	sshsMemoryCheck(descriptionCopy, __func__);

	return (descriptionCopy);
}
