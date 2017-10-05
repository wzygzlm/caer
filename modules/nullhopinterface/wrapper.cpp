/* C wrapper to NullHop interface
 *  Author: federico.corradi@inilabs.com
 */
#include "wrapper.h"
#include "zs_driver.hpp"

extern "C" {

zs_driver* newzs_driver(char * stringa) {
	return new zs_driver(stringa);
}

int zs_driver_classify_image(zs_driver* v, caerFrameEventPacketConst picture){
	return v->classify_image(picture);
}

}
