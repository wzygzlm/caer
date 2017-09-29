/* C wrapper to caffe interface
 *  Author: federico.corradi@gmail.com
 */
#ifndef __WRAPPER_H
#define __WRAPPER_H
#include <stdint.h>
#include <libcaer/events/frame.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MyCaffe MyCaffe;

MyCaffe* newMyCaffe();

void MyCaffe_file_set(MyCaffe* v, caerFrameEventPacketConst frameIn, bool thr, bool printOut,
						bool showactivations, bool norminput);

char * MyCaffe_file_get(MyCaffe* v);

void MyCaffe_init_network(MyCaffe *v, int lowPass);

void deleteMyCaffe(MyCaffe* v);

#ifdef __cplusplus
}
#endif
#endif
