#ifndef DAVIS_COMMON_H_
#define DAVIS_COMMON_H_

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/special.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>
#include <libcaer/events/imu6.h>
#include <libcaer/events/sample.h>
#include <libcaer/devices/davis.h>

bool caerInputDAVISInit(caerModuleData moduleData, uint16_t deviceType);
void caerInputDAVISExit(caerModuleData moduleData);
void caerInputDAVISRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);

#endif /* DAVIS_COMMON_H_ */
