#ifndef CONFIG_SERVER_H_
#define CONFIG_SERVER_H_

#include "caer-sdk/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

// Control message format: 1 byte ACTION, 1 byte TYPE, 2 bytes EXTRA_LEN,
// 2 bytes NODE_LEN, 2 bytes KEY_LEN, 2 bytes VALUE_LEN, then up to 4086
// bytes split between EXTRA, NODE, KEY, VALUE (with 4 bytes for NUL).
// Basically: (EXTRA_LEN + NODE_LEN + KEY_LEN + VALUE_LEN) <= 4086.
// EXTRA, NODE, KEY, VALUE have to be NUL terminated, and their length
// must include the NUL termination byte.
// This results in a maximum message size of 4096 bytes (4KB).
#define CAER_CONFIG_SERVER_BUFFER_SIZE 4096
#define CAER_CONFIG_SERVER_HEADER_SIZE 10

enum caer_config_actions {
	CAER_CONFIG_NODE_EXISTS     = 0,
	CAER_CONFIG_ATTR_EXISTS     = 1,
	CAER_CONFIG_GET             = 2,
	CAER_CONFIG_PUT             = 3,
	CAER_CONFIG_ERROR           = 4,
	CAER_CONFIG_GET_CHILDREN    = 5,
	CAER_CONFIG_GET_ATTRIBUTES  = 6,
	CAER_CONFIG_GET_TYPES       = 7,
	CAER_CONFIG_GET_RANGES      = 8,
	CAER_CONFIG_GET_FLAGS       = 9,
	CAER_CONFIG_GET_DESCRIPTION = 10,
	CAER_CONFIG_ADD_MODULE      = 11,
	CAER_CONFIG_REMOVE_MODULE   = 12,
};

void caerConfigServerStart(void);
void caerConfigServerStop(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_SERVER_H_ */
