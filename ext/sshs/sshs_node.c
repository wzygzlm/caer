#include "sshs_internal.h"
#include "ext/uthash/uthash.h"
#include "ext/uthash/utlist.h"

struct sshs_node {
	char *name;
	char *path;
	sshsNode parent;
	sshsNode children;
	sshsNodeAttr attributes;
	sshsNodeListener nodeListeners;
	sshsNodeAttrListener attrListeners;
	mtx_shared_t traversal_lock;
	mtx_shared_t node_lock;
	UT_hash_handle hh;
};

struct sshs_node_attr {
	UT_hash_handle hh;
	union sshs_node_attr_range min;
	union sshs_node_attr_range max;
	enum sshs_node_attr_flags flags;
	union sshs_node_attr_value value;
	enum sshs_node_attr_value_type value_type;
	char key[];
};

struct sshs_node_listener {
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode);
	void *userData;
	sshsNodeListener next;
};

struct sshs_node_attr_listener {
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
	void *userData;
	sshsNodeAttrListener next;
};

static int sshsNodeCmp(const void *a, const void *b);
static bool sshsNodeCheckRange(enum sshs_node_attr_value_type type, union sshs_node_attr_value value,
	union sshs_node_attr_range min, union sshs_node_attr_range max);
static bool sshsNodeCheckAttributeValueChanged(enum sshs_node_attr_value_type type, union sshs_node_attr_value oldValue,
	union sshs_node_attr_value newValue);
static int sshsNodeAttrCmp(const void *a, const void *b);
static sshsNodeAttr *sshsNodeGetAttributes(sshsNode node, size_t *numAttributes);
static const char *sshsNodeXMLWhitespaceCallback(mxml_node_t *node, int where);
static void sshsNodeToXML(sshsNode node, int outFd, bool recursive, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength);
static mxml_node_t *sshsNodeGenerateXML(sshsNode node, bool recursive, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength);
static mxml_node_t **sshsNodeXMLFilterChildNodes(mxml_node_t *node, const char *nodeName, size_t *numChildren);
static bool sshsNodeFromXML(sshsNode node, int inFd, bool recursive, bool strict);
static void sshsNodeConsumeXML(sshsNode node, mxml_node_t *content, bool recursive);

sshsNode sshsNodeNew(const char *nodeName, sshsNode parent) {
	sshsNode newNode = malloc(sizeof(*newNode));
	SSHS_MALLOC_CHECK_EXIT(newNode);
	memset(newNode, 0, sizeof(*newNode));

	// Allocate full copy of string, so that we control the memory.
	size_t nameLength = strlen(nodeName);
	newNode->name = malloc(nameLength + 1);
	SSHS_MALLOC_CHECK_EXIT(newNode->name);

	// Copy the string.
	strcpy(newNode->name, nodeName);

	newNode->parent = parent;
	newNode->children = NULL;
	newNode->attributes = NULL;
	newNode->nodeListeners = NULL;
	newNode->attrListeners = NULL;

	if (mtx_shared_init(&newNode->traversal_lock) != thrd_success) {
		// Locks are critical for thread-safety.
		char errorMsg[4096];
		snprintf(errorMsg, 4096, "Failed to initialize traversal_lock for node: '%s'.", nodeName);
		(*sshsGetGlobalErrorLogCallback())(errorMsg);
		exit(EXIT_FAILURE);
	}
	if (mtx_shared_init(&newNode->node_lock) != thrd_success) {
		// Locks are critical for thread-safety.
		char errorMsg[4096];
		snprintf(errorMsg, 4096, "Failed to initialize node_lock for node: '%s'.", nodeName);
		(*sshsGetGlobalErrorLogCallback())(errorMsg);
		exit(EXIT_FAILURE);
	}

	// Path is based on parent.
	if (parent != NULL) {
		// Either allocate string copy for full path.
		size_t pathLength = strlen(sshsNodeGetPath(parent)) + nameLength + 1; // + 1 for trailing slash
		newNode->path = malloc(pathLength + 1);
		SSHS_MALLOC_CHECK_EXIT(newNode->path);

		// Generate string.
		snprintf(newNode->path, pathLength + 1, "%s%s/", sshsNodeGetPath(parent), nodeName);
	}
	else {
		// Or the root has an empty, constant path.
		newNode->path = "/";
	}

	return (newNode);
}

const char *sshsNodeGetName(sshsNode node) {
	return (node->name);
}

const char *sshsNodeGetPath(sshsNode node) {
	return (node->path);
}

sshsNode sshsNodeGetParent(sshsNode node) {
	return (node->parent);
}

sshsNode sshsNodeAddChild(sshsNode node, const char *childName) {
	mtx_shared_lock_exclusive(&node->traversal_lock);

	// Atomic putIfAbsent: returns null if nothing was there before and the
	// node is the new one, or it returns the old node if already present.
	sshsNode child = NULL, newChild = NULL;
	HASH_FIND_STR(node->children, childName, child);

	if (child == NULL) {
		// Create new child node with appropriate name and parent.
		newChild = sshsNodeNew(childName, node);

		// No node present, let's add it.
		HASH_ADD_KEYPTR(hh, node->children, sshsNodeGetName(newChild), strlen(sshsNodeGetName(newChild)), newChild);
	}

	mtx_shared_unlock_exclusive(&node->traversal_lock);

	// If null was returned, then nothing was in the map beforehand, and
	// thus the new node 'child' is the node that's now in the map.
	if (child == NULL) {
		// Listener support (only on new addition!).
		mtx_shared_lock_shared(&node->node_lock);

		sshsNodeListener l;
		LL_FOREACH(node->nodeListeners, l)
		{
			l->node_changed(node, l->userData, SSHS_CHILD_NODE_ADDED, newChild);
		}

		mtx_shared_unlock_shared(&node->node_lock);

		return (newChild);
	}

	return (child);
}

sshsNode sshsNodeGetChild(sshsNode node, const char* childName) {
	mtx_shared_lock_shared(&node->traversal_lock);

	sshsNode child;
	HASH_FIND_STR(node->children, childName, child);

	mtx_shared_unlock_shared(&node->traversal_lock);

	// Either null or an always valid value.
	return (child);
}

static int sshsNodeCmp(const void *a, const void *b) {
	const sshsNode *aa = a;
	const sshsNode *bb = b;

	return (strcmp(sshsNodeGetName(*aa), sshsNodeGetName(*bb)));
}

sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren) {
	mtx_shared_lock_shared(&node->traversal_lock);

	size_t childrenCount = HASH_COUNT(node->children);

	// If none, exit gracefully.
	if (childrenCount == 0) {
		mtx_shared_unlock_shared(&node->traversal_lock);

		*numChildren = 0;
		return (NULL);
	}

	sshsNode *children = malloc(childrenCount * sizeof(*children));
	SSHS_MALLOC_CHECK_EXIT(children);

	size_t i = 0;
	for (sshsNode n = node->children; n != NULL; n = n->hh.next) {
		children[i++] = n;
	}

	mtx_shared_unlock_shared(&node->traversal_lock);

	// Sort by name.
	qsort(children, childrenCount, sizeof(sshsNode), &sshsNodeCmp);

	*numChildren = childrenCount;
	return (children);
}

void sshsNodeAddNodeListener(sshsNode node, void *userData,
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode)) {
	sshsNodeListener listener = malloc(sizeof(*listener));
	SSHS_MALLOC_CHECK_EXIT(listener);

	listener->node_changed = node_changed;
	listener->userData = userData;

	mtx_shared_lock_exclusive(&node->node_lock);

	// Search if we don't already have this exact listener, to avoid duplicates.
	bool found = false;

	sshsNodeListener curr;
	LL_FOREACH(node->nodeListeners, curr)
	{
		if (curr->node_changed == node_changed && curr->userData == userData) {
			found = true;
		}
	}

	if (!found) {
		LL_PREPEND(node->nodeListeners, listener);
	}
	else {
		free(listener);
	}

	mtx_shared_unlock_exclusive(&node->node_lock);
}

void sshsNodeRemoveNodeListener(sshsNode node, void *userData,
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode)) {
	mtx_shared_lock_exclusive(&node->node_lock);

	sshsNodeListener curr, curr_tmp;
	LL_FOREACH_SAFE(node->nodeListeners, curr, curr_tmp)
	{
		if (curr->node_changed == node_changed && curr->userData == userData) {
			LL_DELETE(node->nodeListeners, curr);
			free(curr);
		}
	}

	mtx_shared_unlock_exclusive(&node->node_lock);
}

void sshsNodeRemoveAllNodeListeners(sshsNode node) {
	mtx_shared_lock_exclusive(&node->node_lock);

	sshsNodeListener curr, curr_tmp;
	LL_FOREACH_SAFE(node->nodeListeners, curr, curr_tmp)
	{
		LL_DELETE(node->nodeListeners, curr);
		free(curr);
	}

	mtx_shared_unlock_exclusive(&node->node_lock);
}

void sshsNodeAddAttributeListener(sshsNode node, void *userData,
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue)) {
	sshsNodeAttrListener listener = malloc(sizeof(*listener));
	SSHS_MALLOC_CHECK_EXIT(listener);

	listener->attribute_changed = attribute_changed;
	listener->userData = userData;

	mtx_shared_lock_exclusive(&node->node_lock);

	// Search if we don't already have this exact listener, to avoid duplicates.
	bool found = false;

	sshsNodeAttrListener curr;
	LL_FOREACH(node->attrListeners, curr)
	{
		if (curr->attribute_changed == attribute_changed && curr->userData == userData) {
			found = true;
		}
	}

	if (!found) {
		LL_PREPEND(node->attrListeners, listener);
	}
	else {
		free(listener);
	}

	mtx_shared_unlock_exclusive(&node->node_lock);
}

void sshsNodeRemoveAttributeListener(sshsNode node, void *userData,
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue)) {
	mtx_shared_lock_exclusive(&node->node_lock);

	sshsNodeAttrListener curr, curr_tmp;
	LL_FOREACH_SAFE(node->attrListeners, curr, curr_tmp)
	{
		if (curr->attribute_changed == attribute_changed && curr->userData == userData) {
			LL_DELETE(node->attrListeners, curr);
			free(curr);
		}
	}

	mtx_shared_unlock_exclusive(&node->node_lock);
}

void sshsNodeRemoveAllAttributeListeners(sshsNode node) {
	mtx_shared_lock_exclusive(&node->node_lock);

	sshsNodeAttrListener curr, curr_tmp;
	LL_FOREACH_SAFE(node->attrListeners, curr, curr_tmp)
	{
		LL_DELETE(node->attrListeners, curr);
		free(curr);
	}

	mtx_shared_unlock_exclusive(&node->node_lock);
}

void sshsNodeTransactionLock(sshsNode node) {
	mtx_shared_lock_exclusive(&node->node_lock);
}

void sshsNodeTransactionUnlock(sshsNode node) {
	mtx_shared_unlock_exclusive(&node->node_lock);
}

static bool sshsNodeCheckRange(enum sshs_node_attr_value_type type, union sshs_node_attr_value value,
	union sshs_node_attr_range min, union sshs_node_attr_range max) {
	// Check limits: use integer for all ints, double for float/double,
	// and for strings take the length. Bool has no limits!
	switch (type) {
		case SSHS_BOOL:
			// No check for bool. Always true.
			return (true);

		case SSHS_BYTE:
			return (value.ibyte >= min.i && value.ibyte <= max.i);

		case SSHS_SHORT:
			return (value.ishort >= min.i && value.ishort <= max.i);

		case SSHS_INT:
			return (value.iint >= min.i && value.iint <= max.i);

		case SSHS_LONG:
			return (value.ilong >= min.i && value.ilong <= max.i);

		case SSHS_FLOAT:
			return (value.ffloat >= (float) min.d && value.ffloat <= (float) max.d);

		case SSHS_DOUBLE:
			return (value.ddouble >= min.d && value.ddouble <= max.d);

		case SSHS_STRING: {
			size_t stringLength = strlen(value.string);
			return (stringLength >= (size_t) min.i && stringLength <= (size_t) max.i);
		}

		case SSHS_UNKNOWN:
		default:
			return (false); // UNKNOWN TYPE.
	}
}

void sshsNodeCreateAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value defaultValue, union sshs_node_attr_range minValue, union sshs_node_attr_range maxValue,
	enum sshs_node_attr_flags flags) {
	// Check that value conforms to limits.
	if (!sshsNodeCheckRange(type, defaultValue, minValue, maxValue)) {
		// Fail on wrong default value. Must be within range!
		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodeCreateAttribute(): attribute '%s' of type '%s' has default value '%s' that is out of specified range. "
				"Please make sure the default value is within the given range!", key,
			sshsHelperTypeToStringConverter(type), sshsHelperValueToStringConverter(type, defaultValue));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	size_t keyLength = strlen(key);
	sshsNodeAttr newAttr = malloc(sizeof(*newAttr) + keyLength + 1);
	SSHS_MALLOC_CHECK_EXIT(newAttr);
	memset(newAttr, 0, sizeof(*newAttr));

	if (type == SSHS_STRING) {
		// Make a copy of the string so we own the memory internally.
		char *valueCopy = strdup(defaultValue.string);
		SSHS_MALLOC_CHECK_EXIT(valueCopy);

		newAttr->value.string = valueCopy;
	}
	else {
		newAttr->value = defaultValue;
	}

	newAttr->min = minValue;
	newAttr->max = maxValue;
	newAttr->flags = flags;

	newAttr->value_type = type;
	strcpy(newAttr->key, key);

	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	mtx_shared_lock_exclusive(&node->node_lock);

	sshsNodeAttr oldAttr;
	HASH_FIND(hh, node->attributes, &newAttr->value_type, fullKeyLength, oldAttr);

	bool attrValueChanged = false;

	// Add if not present. Else update value (below).
	if (oldAttr == NULL) {
		HASH_ADD(hh, node->attributes, value_type, fullKeyLength, newAttr);
	}
	else {
		// If value was present, update its range and flags, and make sure
		// the current value is still within range.
		oldAttr->min = minValue;
		oldAttr->max = maxValue;
		oldAttr->flags = flags;

		// If old value is out of range, replace with new default value.
		if (!sshsNodeCheckRange(type, oldAttr->value, minValue, maxValue)) {
			attrValueChanged = true;

			oldAttr->value = newAttr->value;
		}
		else {
			// Value still fine, free unused memory (string value).
			if (type == SSHS_STRING) {
				free(newAttr->value.string);
			}
		}

		free(newAttr);
	}

	mtx_shared_unlock_exclusive(&node->node_lock);

	if (oldAttr == NULL) {
		// Listener support. Call only on change, which is always the case here.
		mtx_shared_lock_shared(&node->node_lock);

		sshsNodeAttrListener l;
		LL_FOREACH(node->attrListeners, l)
		{
			l->attribute_changed(node, l->userData, SSHS_ATTRIBUTE_ADDED, key, type, defaultValue);
		}

		mtx_shared_unlock_shared(&node->node_lock);
	}
	else {
		// Let's check if anything changed with this update and call
		// the appropriate listeners if needed.
		if (attrValueChanged) {
			// Listener support. Call only on change, which is always the case here.
			mtx_shared_lock_shared(&node->node_lock);

			sshsNodeAttrListener l;
			LL_FOREACH(node->attrListeners, l)
			{
				l->attribute_changed(node, l->userData, SSHS_ATTRIBUTE_MODIFIED, key, type, defaultValue);
			}

			mtx_shared_unlock_shared(&node->node_lock);
		}
	}
}

bool sshsNodeAttributeExists(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	size_t keyLength = strlen(key);
	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	uint8_t searchKey[fullKeyLength];

	memcpy(searchKey, &type, sizeof(enum sshs_node_attr_value_type));
	strcpy((char *) (searchKey + sizeof(enum sshs_node_attr_value_type)), key);

	mtx_shared_lock_shared(&node->node_lock);

	sshsNodeAttr attr;
	HASH_FIND(hh, node->attributes, searchKey, fullKeyLength, attr);

	mtx_shared_unlock_shared(&node->node_lock);

	// If attr == NULL, the specified attribute was not found.
	if (attr == NULL) {
		return (false);
	}

	// The specified attribute exists.
	return (true);
}

bool sshsNodePutAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value value) {
	size_t keyLength = strlen(key);
	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	uint8_t searchKey[fullKeyLength];

	memcpy(searchKey, &type, sizeof(enum sshs_node_attr_value_type));
	strcpy((char *) (searchKey + sizeof(enum sshs_node_attr_value_type)), key);

	mtx_shared_lock_exclusive(&node->node_lock);

	sshsNodeAttr attr;
	HASH_FIND(hh, node->attributes, searchKey, fullKeyLength, attr);

	// Verify that a valid attribute exists.
	if (attr == NULL) {
		mtx_shared_unlock_exclusive(&node->node_lock);

		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodePutAttribute(): attribute '%s' of type '%s' not present, please create it first.", key,
			sshsHelperTypeToStringConverter(type));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	// Value must be present, so update old one, after checking range and flags.
	if (attr->flags == SSHS_FLAGS_READ_ONLY) {
		// Read-only flag set, cannot put new value!
		mtx_shared_unlock_exclusive(&node->node_lock);
		return (false);
	}

	if (!sshsNodeCheckRange(type, value, attr->min, attr->max)) {
		// New value out of range, cannot put new value!
		mtx_shared_unlock_exclusive(&node->node_lock);
		return (false);
	}

	// Key and valueType have to be the same, so only update the value
	// itself with the new one, and save the old one for later.
	union sshs_node_attr_value attrValueOld = attr->value;

	if (attr->flags != SSHS_FLAGS_NOTIFY_ONLY) {
		if (type == SSHS_STRING) {
			// Make a copy of the string so we own the memory internally.
			char *valueCopy = strdup(value.string);
			SSHS_MALLOC_CHECK_EXIT(valueCopy);

			attr->value.string = valueCopy;
		}
		else {
			attr->value = value;
		}
	}

	mtx_shared_unlock_exclusive(&node->node_lock);

	// Let's check if anything changed with this update and call
	// the appropriate listeners if needed.
	if (sshsNodeCheckAttributeValueChanged(type, attrValueOld, value)) {
		// Listener support. Call only on change, which is always the case here.
		mtx_shared_lock_shared(&node->node_lock);

		sshsNodeAttrListener l;
		LL_FOREACH(node->attrListeners, l)
		{
			l->attribute_changed(node, l->userData, SSHS_ATTRIBUTE_MODIFIED, key, type, value);
		}

		mtx_shared_unlock_shared(&node->node_lock);
	}

	// Free oldAttr's string memory, not used anymore.
	if (type == SSHS_STRING) {
		free(attrValueOld.string);
	}

	return (true);
}

static bool sshsNodeCheckAttributeValueChanged(enum sshs_node_attr_value_type type, union sshs_node_attr_value oldValue,
	union sshs_node_attr_value newValue) {
	// Check that the two values changed, that there is a difference between then.
	switch (type) {
		case SSHS_BOOL:
			return (oldValue.boolean != newValue.boolean);

		case SSHS_BYTE:
			return (oldValue.ibyte != newValue.ibyte);

		case SSHS_SHORT:
			return (oldValue.ishort != newValue.ishort);

		case SSHS_INT:
			return (oldValue.iint != newValue.iint);

		case SSHS_LONG:
			return (oldValue.ilong != newValue.ilong);

		case SSHS_FLOAT:
			return (oldValue.ffloat != newValue.ffloat);

		case SSHS_DOUBLE:
			return (oldValue.ddouble != newValue.ddouble);

		case SSHS_STRING:
			return (strcmp(oldValue.string, newValue.string) != 0);

		case SSHS_UNKNOWN:
		default:
			return (false);
	}
}

union sshs_node_attr_value sshsNodeGetAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	size_t keyLength = strlen(key);
	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	uint8_t searchKey[fullKeyLength];

	memcpy(searchKey, &type, sizeof(enum sshs_node_attr_value_type));
	strcpy((char *) (searchKey + sizeof(enum sshs_node_attr_value_type)), key);

	mtx_shared_lock_shared(&node->node_lock);

	sshsNodeAttr attr;
	HASH_FIND(hh, node->attributes, searchKey, fullKeyLength, attr);

	// Verify that a valid attribute exists.
	if (attr == NULL) {
		mtx_shared_unlock_shared(&node->node_lock);

		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodeGetAttribute(): attribute '%s' of type '%s' not present, please create it first.", key,
			sshsHelperTypeToStringConverter(type));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	// Copy the value while still holding the lock, to ensure accessing it is
	// still possible and the value behind it valid.
	union sshs_node_attr_value value = attr->value;

	// For strings, make a copy on the heap to give out.
	if (type == SSHS_STRING) {
		char *valueCopy = strdup(value.string);
		SSHS_MALLOC_CHECK_EXIT(valueCopy);

		value.string = valueCopy;
	}

	mtx_shared_unlock_shared(&node->node_lock);

	// Return the final value.
	return (value);
}

static int sshsNodeAttrCmp(const void *a, const void *b) {
	const sshsNodeAttr *aa = a;
	const sshsNodeAttr *bb = b;

	return (strcmp((*aa)->key, (*bb)->key));
}

static sshsNodeAttr *sshsNodeGetAttributes(sshsNode node, size_t *numAttributes) {
	mtx_shared_lock_shared(&node->node_lock);

	size_t attributeCount = HASH_COUNT(node->attributes);

	// If none, exit gracefully.
	if (attributeCount == 0) {
		mtx_shared_unlock_shared(&node->node_lock);

		*numAttributes = 0;
		return (NULL);
	}

	sshsNodeAttr *attributes = malloc(attributeCount * sizeof(*attributes));
	SSHS_MALLOC_CHECK_EXIT(attributes);

	size_t i = 0;
	for (sshsNodeAttr a = node->attributes; a != NULL; a = a->hh.next) {
		attributes[i++] = a;
	}

	mtx_shared_unlock_shared(&node->node_lock);

	// Sort by name.
	qsort(attributes, attributeCount, sizeof(sshsNodeAttr), &sshsNodeAttrCmp);

	*numAttributes = attributeCount;
	return (attributes);
}

void sshsNodeCreateBool(sshsNode node, const char *key, bool defaultValue, enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_BOOL, (union sshs_node_attr_value ) { .boolean = defaultValue },
		(union sshs_node_attr_range ) { .i = -1 }, (union sshs_node_attr_range ) { .i = -1 }, flags);
}

void sshsNodePutBool(sshsNode node, const char *key, bool value) {
	sshsNodePutAttribute(node, key, SSHS_BOOL, (union sshs_node_attr_value ) { .boolean = value });
}

bool sshsNodeGetBool(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_BOOL).boolean);
}

void sshsNodeCreateByte(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_BYTE, (union sshs_node_attr_value ) { .ibyte = defaultValue },
		(union sshs_node_attr_range ) { .i = minValue }, (union sshs_node_attr_range ) { .i = maxValue }, flags);
}

void sshsNodePutByte(sshsNode node, const char *key, int8_t value) {
	sshsNodePutAttribute(node, key, SSHS_BYTE, (union sshs_node_attr_value ) { .ibyte = value });
}

int8_t sshsNodeGetByte(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_BYTE).ibyte);
}

void sshsNodeCreateShort(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_SHORT, (union sshs_node_attr_value ) { .ishort = defaultValue },
		(union sshs_node_attr_range ) { .i = minValue }, (union sshs_node_attr_range ) { .i = maxValue }, flags);
}

void sshsNodePutShort(sshsNode node, const char *key, int16_t value) {
	sshsNodePutAttribute(node, key, SSHS_SHORT, (union sshs_node_attr_value ) { .ishort = value });
}

int16_t sshsNodeGetShort(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_SHORT).ishort);
}

void sshsNodeCreateInt(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_INT, (union sshs_node_attr_value ) { .iint = defaultValue },
		(union sshs_node_attr_range ) { .i = minValue }, (union sshs_node_attr_range ) { .i = maxValue }, flags);
}

void sshsNodePutInt(sshsNode node, const char *key, int32_t value) {
	sshsNodePutAttribute(node, key, SSHS_INT, (union sshs_node_attr_value ) { .iint = value });
}

int32_t sshsNodeGetInt(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_INT).iint);
}

void sshsNodeCreateLong(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_LONG, (union sshs_node_attr_value ) { .ilong = defaultValue },
		(union sshs_node_attr_range ) { .i = minValue }, (union sshs_node_attr_range ) { .i = maxValue }, flags);
}

void sshsNodePutLong(sshsNode node, const char *key, int64_t value) {
	sshsNodePutAttribute(node, key, SSHS_LONG, (union sshs_node_attr_value ) { .ilong = value });
}

int64_t sshsNodeGetLong(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_LONG).ilong);
}

void sshsNodeCreateFloat(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_FLOAT, (union sshs_node_attr_value ) { .ffloat = defaultValue },
		(union sshs_node_attr_range ) { .d = minValue }, (union sshs_node_attr_range ) { .d = maxValue }, flags);
}

void sshsNodePutFloat(sshsNode node, const char *key, float value) {
	sshsNodePutAttribute(node, key, SSHS_FLOAT, (union sshs_node_attr_value ) { .ffloat = value });
}

float sshsNodeGetFloat(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_FLOAT).ffloat);
}

void sshsNodeCreateDouble(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_DOUBLE, (union sshs_node_attr_value ) { .ddouble = defaultValue },
		(union sshs_node_attr_range ) { .d = minValue }, (union sshs_node_attr_range ) { .d = maxValue }, flags);
}

void sshsNodePutDouble(sshsNode node, const char *key, double value) {
	sshsNodePutAttribute(node, key, SSHS_DOUBLE, (union sshs_node_attr_value ) { .ddouble = value });
}

double sshsNodeGetDouble(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_DOUBLE).ddouble);
}

void sshsNodeCreateString(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	enum sshs_node_attr_flags flags) {
	sshsNodeCreateAttribute(node, key, SSHS_STRING, (union sshs_node_attr_value ) { .string = (char *) defaultValue },
		(union sshs_node_attr_range ) { .i = (int64_t) minLength }, (union sshs_node_attr_range ) { .i =
					(int64_t) maxLength }, flags);
}

void sshsNodePutString(sshsNode node, const char *key, const char *value) {
	sshsNodePutAttribute(node, key, SSHS_STRING, (union sshs_node_attr_value ) { .string = (char *) value });
}

// This is a copy of the string on the heap, remember to free() when done!
char *sshsNodeGetString(sshsNode node, const char *key) {
	return (sshsNodeGetAttribute(node, key, SSHS_STRING).string);
}

void sshsNodeExportNodeToXML(sshsNode node, int outFd, const char **filterKeys, size_t filterKeysLength) {
	sshsNodeToXML(node, outFd, false, filterKeys, filterKeysLength, NULL, 0);
}

void sshsNodeExportSubTreeToXML(sshsNode node, int outFd, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength) {
	sshsNodeToXML(node, outFd, true, filterKeys, filterKeysLength, filterNodes, filterNodesLength);
}

#define INDENT_MAX_LEVEL 20
#define INDENT_SPACES 4
static char spaces[(INDENT_MAX_LEVEL * INDENT_SPACES) + 1] =
	"                                                                                ";

static const char *sshsNodeXMLWhitespaceCallback(mxml_node_t *node, int where) {
	const char *name = mxmlGetElement(node);
	size_t level = 0;

	// Calculate indentation level always.
	for (mxml_node_t *parent = mxmlGetParent(node); parent != NULL; parent = mxmlGetParent(parent)) {
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

	return (NULL);
}

static void sshsNodeToXML(sshsNode node, int outFd, bool recursive, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength) {
	mxml_node_t *root = mxmlNewElement(MXML_NO_PARENT, "sshs");
	mxmlElementSetAttr(root, "version", "1.0");
	mxmlAdd(root, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		sshsNodeGenerateXML(node, recursive, filterKeys, filterKeysLength, filterNodes, filterNodesLength));

	// Disable wrapping
	mxmlSetWrapMargin(0);

	// Output to file descriptor.
	mxmlSaveFd(root, outFd, &sshsNodeXMLWhitespaceCallback);

	mxmlDelete(root);
}

static mxml_node_t *sshsNodeGenerateXML(sshsNode node, bool recursive, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength) {
	mxml_node_t *this = mxmlNewElement(MXML_NO_PARENT, "node");

	// First this node's name and full path.
	mxmlElementSetAttr(this, "name", sshsNodeGetName(node));
	mxmlElementSetAttr(this, "path", sshsNodeGetPath(node));

	size_t numAttributes;
	sshsNodeAttr *attributes = sshsNodeGetAttributes(node, &numAttributes);

	// Then it's attributes (key:value pairs).
	for (size_t i = 0; i < numAttributes; i++) {
		bool isFilteredOut = false;

		// Verify that the key is not filtered out.
		for (size_t fk = 0; fk < filterKeysLength; fk++) {
			if (strcmp(attributes[i]->key, filterKeys[fk]) == 0) {
				// Matches, don't add this attribute.
				isFilteredOut = true;
				break;
			}
		}

		if (isFilteredOut) {
			continue;
		}

		const char *type = sshsHelperTypeToStringConverter(attributes[i]->value_type);
		char *value = sshsHelperValueToStringConverter(attributes[i]->value_type, attributes[i]->value);
		SSHS_MALLOC_CHECK_EXIT(value);

		mxml_node_t *attr = mxmlNewElement(this, "attr");
		mxmlElementSetAttr(attr, "key", attributes[i]->key);
		mxmlElementSetAttr(attr, "type", type);
		mxmlNewText(attr, 0, value);

		free(value);
	}

	free(attributes);

	// And lastly recurse down to the children.
	if (recursive) {
		size_t numChildren;
		sshsNode *children = sshsNodeGetChildren(node, &numChildren);

		for (size_t i = 0; i < numChildren; i++) {
			// First check that this child node is not filtered out.
			bool isFilteredOut = false;

			// Verify that the node is not filtered out.
			for (size_t fn = 0; fn < filterNodesLength; fn++) {
				if (strcmp(sshsNodeGetName(children[i]), filterNodes[fn]) == 0) {
					// Matches, don't process this node.
					isFilteredOut = true;
					break;
				}
			}

			if (isFilteredOut) {
				continue;
			}

			mxml_node_t *child = sshsNodeGenerateXML(children[i], recursive, filterKeys, filterKeysLength, filterNodes,
				filterNodesLength);

			if (mxmlGetFirstChild(child) != NULL) {
				mxmlAdd(this, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, child);
			}
			else {
				// Free memory if not adding.
				mxmlDelete(child);
			}
		}

		free(children);
	}

	return (this);
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

	for (mxml_node_t *current = mxmlGetFirstChild(node); current != NULL; current = mxmlGetNextSibling(current)) {
		const char *name = mxmlGetElement(current);

		if (name != NULL && strcmp(name, nodeName) == 0) {
			matchedChildren++;
		}
	}

	// If none, exit gracefully.
	if (matchedChildren == 0) {
		*numChildren = 0;
		return (NULL);
	}

	// Now allocate appropriate memory for list.
	mxml_node_t **filteredNodes = malloc(matchedChildren * sizeof(mxml_node_t *));
	SSHS_MALLOC_CHECK_EXIT(filteredNodes);

	// Go thorough again and collect the matching nodes.
	size_t i = 0;
	for (mxml_node_t *current = mxmlGetFirstChild(node); current != NULL; current = mxmlGetNextSibling(current)) {
		const char *name = mxmlGetElement(current);

		if (name != NULL && strcmp(name, nodeName) == 0) {
			filteredNodes[i++] = current;
		}
	}

	*numChildren = matchedChildren;
	return (filteredNodes);
}

static bool sshsNodeFromXML(sshsNode node, int inFd, bool recursive, bool strict) {
	mxml_node_t *root = mxmlLoadFd(NULL, inFd, MXML_OPAQUE_CALLBACK);

	if (root == NULL) {
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

		if (rootNodeName == NULL || strcmp(rootNodeName, sshsNodeGetName(node)) != 0) {
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

		if (key == NULL || type == NULL) {
			continue;
		}

		// Get the needed values.
		const char *value = mxmlGetOpaque(attrChildren[i]);

		if (!sshsNodeStringToNodeConverter(node, key, type, value)) {
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

			if (childName == NULL) {
				continue;
			}

			// Get the child node.
			sshsNode childNode = sshsNodeGetChild(node, childName);

			// If not existing, try to create.
			if (childNode == NULL) {
				childNode = sshsNodeAddChild(node, childName);
			}

			// And call recursively.
			sshsNodeConsumeXML(childNode, nodeChildren[i], recursive);
		}

		free(nodeChildren);
	}
}

bool sshsNodeStringToNodeConverter(sshsNode node, const char *key, const char *typeStr, const char *valueStr) {
	// Parse the values according to type and put them in the node.
	enum sshs_node_attr_value_type type;
	type = sshsHelperStringToTypeConverter(typeStr);

	union sshs_node_attr_value value;
	bool conversionSuccess = sshsHelperStringToValueConverter(type, valueStr, &value);

	if ((type == SSHS_UNKNOWN) || !conversionSuccess) {
		return (false);
	}

	sshsNodePutAttribute(node, key, type, value);

	// Free string copy from helper.
	if (type == SSHS_STRING) {
		free(value.string);
	}

	return (true);
}

const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames) {
	size_t numChildren;
	sshsNode *children = sshsNodeGetChildren(node, &numChildren);

	if (children == NULL) {
		*numNames = 0;
		return (NULL);
	}

	const char **childNames = malloc(numChildren * sizeof(*childNames));
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
	size_t numAttributes;
	sshsNodeAttr *attributes = sshsNodeGetAttributes(node, &numAttributes);

	if (attributes == NULL) {
		*numKeys = 0;
		return (NULL);
	}

	const char **attributeKeys = malloc(numAttributes * sizeof(*attributeKeys));
	SSHS_MALLOC_CHECK_EXIT(attributeKeys);

	// Copy pointers to key string over. Safe because attributes are never deleted.
	for (size_t i = 0; i < numAttributes; i++) {
		attributeKeys[i] = attributes[i]->key;
	}

	free(attributes);

	*numKeys = numAttributes;
	return (attributeKeys);
}

enum sshs_node_attr_value_type *sshsNodeGetAttributeTypes(sshsNode node, const char *key, size_t *numTypes) {
	size_t numAttributes;
	sshsNodeAttr *attributes = sshsNodeGetAttributes(node, &numAttributes);

	if (attributes == NULL) {
		*numTypes = 0;
		return (NULL);
	}

	// There are at most 8 types for one specific attribute key.
	enum sshs_node_attr_value_type *attributeTypes = malloc(8 * sizeof(*attributeTypes));
	SSHS_MALLOC_CHECK_EXIT(attributeTypes);

	// Check each attribute if it matches, and save its type if true.
	size_t typeCounter = 0;

	for (size_t i = 0; i < numAttributes; i++) {
		if (strcmp(key, attributes[i]->key) == 0) {
			attributeTypes[typeCounter++] = attributes[i]->value_type;
		}
	}

	free(attributes);

	// If we found nothing, return nothing.
	if (typeCounter == 0) {
		free(attributeTypes);
		attributeTypes = NULL;
	}

	*numTypes = typeCounter;
	return (attributeTypes);
}

union sshs_node_attr_range sshsNodeGetAttributeMinRange(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type) {
	size_t keyLength = strlen(key);
	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	uint8_t searchKey[fullKeyLength];

	memcpy(searchKey, &type, sizeof(enum sshs_node_attr_value_type));
	strcpy((char *) (searchKey + sizeof(enum sshs_node_attr_value_type)), key);

	mtx_shared_lock_shared(&node->node_lock);

	sshsNodeAttr attr;
	HASH_FIND(hh, node->attributes, searchKey, fullKeyLength, attr);

	// Verify that a valid attribute exists.
	if (attr == NULL) {
		mtx_shared_unlock_shared(&node->node_lock);

		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodeGetAttributeMinRange(): attribute '%s' of type '%s' not present, please create it first.", key,
			sshsHelperTypeToStringConverter(type));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	union sshs_node_attr_range minRange = attr->min;

	mtx_shared_unlock_shared(&node->node_lock);

	return (minRange);
}

union sshs_node_attr_range sshsNodeGetAttributeMaxRange(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type) {
	size_t keyLength = strlen(key);
	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	uint8_t searchKey[fullKeyLength];

	memcpy(searchKey, &type, sizeof(enum sshs_node_attr_value_type));
	strcpy((char *) (searchKey + sizeof(enum sshs_node_attr_value_type)), key);

	mtx_shared_lock_shared(&node->node_lock);

	sshsNodeAttr attr;
	HASH_FIND(hh, node->attributes, searchKey, fullKeyLength, attr);

	// Verify that a valid attribute exists.
	if (attr == NULL) {
		mtx_shared_unlock_shared(&node->node_lock);

		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodeGetAttributeMaxRange(): attribute '%s' of type '%s' not present, please create it first.", key,
			sshsHelperTypeToStringConverter(type));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	union sshs_node_attr_range maxRange = attr->max;

	mtx_shared_unlock_shared(&node->node_lock);

	return (maxRange);
}

enum sshs_node_attr_flags sshsNodeGetAttributeFlags(sshsNode node, const char *key, enum sshs_node_attr_value_type type) {
	size_t keyLength = strlen(key);
	size_t fullKeyLength = offsetof(struct sshs_node_attr, key) + keyLength
		+ 1- offsetof(struct sshs_node_attr, value_type);

	uint8_t searchKey[fullKeyLength];

	memcpy(searchKey, &type, sizeof(enum sshs_node_attr_value_type));
	strcpy((char *) (searchKey + sizeof(enum sshs_node_attr_value_type)), key);

	mtx_shared_lock_shared(&node->node_lock);

	sshsNodeAttr attr;
	HASH_FIND(hh, node->attributes, searchKey, fullKeyLength, attr);

	// Verify that a valid attribute exists.
	if (attr == NULL) {
		mtx_shared_unlock_shared(&node->node_lock);

		char errorMsg[1024];
		snprintf(errorMsg, 1024,
			"sshsNodeGetAttributeFlags(): attribute '%s' of type '%s' not present, please create it first.", key,
			sshsHelperTypeToStringConverter(type));

		(*sshsGetGlobalErrorLogCallback())(errorMsg);

		// This is a critical usage error that *must* be fixed!
		exit(EXIT_FAILURE);
	}

	enum sshs_node_attr_flags flags = attr->flags;

	mtx_shared_unlock_shared(&node->node_lock);

	return (flags);
}
