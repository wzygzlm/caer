#ifndef SPIRALVIEW_H_
#define SPIRALVIEW_H_

#include "main.h"

#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

#define SPIRALVIEW_SCREEN_WIDTH 400
#define SPIRALVIEW_SCREEN_HEIGHT 400

#define CAMERA_X 128
#define CAMERA_Y 128

#define PIXEL_ZOOM 1

#define FRAME_IMG_DIRECTORY "/tmp/"

//we cut out a quadratic part of the spike image from the rectangular input of the camera
#define SIZE_QUADRATIC_MAP 128

void caerSpiralView(uint16_t moduleID, caerPolarityEventPacket polarity, int classify_img_size, int *packet_hist, int *packet_hist_view, bool * haveimg);
void caerSpiralViewMakeFrame(uint16_t moduleID, int * hist_packet,  caerFrameEventPacket *imagegeneratorFrame, int size);
void caerSpiralViewAddText(uint16_t moduleID, int * hist_packet,  caerFrameEventPacket *imagegeneratorFrame, int size);

#endif /* SPIRALVIEW_H_ */
