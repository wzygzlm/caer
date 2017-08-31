/* C wrapper to caffe interface
 *  Author: federico.corradi@inilabs.com
 */
#include "classify.hpp"
#include "wrapper.h"

extern "C" {

MyCaffe* newMyCaffe() {
	return new MyCaffe();
}

void MyCaffe_file_set(MyCaffe* v, caerFrameEventPacketConst frameIn, bool thr, bool printOut,
	bool showactivations, bool norminput) {
	v->file_set(frameIn, thr, printOut, showactivations, norminput);
}

void MyCaffe_init_network(MyCaffe *v) {
	return v->init_network();
}

void deleteMyCaffe(MyCaffe* v) {
	delete v;
}

}
