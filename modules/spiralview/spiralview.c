/*
 *  accumulates a fixed number of events and generates frames
 *  add spirals
 *  federico.corradi@inilabs.com
 */
#include <limits.h>
#include <float.h>
#include "spiralview.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "modules/statistics/statistics.h"
#include "ext/portable_time.h"
#include "ext/buffers.h"
#include <string.h>
#include <stdio.h>

#include "main.h"
#include <libcaer/events/polarity.h>
#include <libcaer/events/frame.h>

struct spiralview_state {
	//image matrix
	int16_t sizeX;
	int16_t sizeY;
	int threshold;
	// Lorenz parameters
	double x;
	double y;
	double z;
	double a;
	double b;
	double c;
	double t;
};

typedef struct spiralview_state *spiralviewState;

static bool caerSpiralViewInit(caerModuleData moduleData);
static void caerSpiralViewRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerSpiralViewExit(caerModuleData moduleData);
static void caerSpiralViewConfig(caerModuleData moduleData);

static struct caer_module_functions caerSpiralViewFunctions = { .moduleInit = &caerSpiralViewInit, .moduleRun =
	&caerSpiralViewRun, .moduleConfig = NULL, .moduleExit = &caerSpiralViewExit };

void caerSpiralView(uint16_t moduleID, caerPolarityEventPacket polarity, int classify_img_size, int *packet_hist,
	int *packet_hist_view, bool * haveimg) {

	caerModuleData moduleData = caerMainloopFindModule(moduleID, "SpiralView", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return;
	}

	caerModuleSM(&caerSpiralViewFunctions, moduleData, sizeof(struct spiralview_state), 5, polarity, classify_img_size,
		packet_hist, packet_hist_view, haveimg);

	return;
}

static bool caerSpiralViewInit(caerModuleData moduleData) {

	// Ensure numSpikes is set.
	spiralviewState state = moduleData->moduleState;
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "threshold", 200);

	state->x = 0.1;
	state->y = 0.0;
	state->z = 0.0;
	state->a = 10.0;
	state->b = 28.0;
	state->c = 8.0 / 3.0;
	state->t = 0.01;

	return (true);
}

static void caerSpiralViewConfig(caerModuleData moduleData) {

	// Ensure numSpikes is set.
	spiralviewState state = moduleData->moduleState;
	state->threshold = sshsNodeGetInt(moduleData->moduleNode, "threshold");

	return;
}

static void caerSpiralViewExit(caerModuleData moduleData) {
	spiralviewState state = moduleData->moduleState;
	return;
}

// add spirals
static bool add_spiral_image_map(spiralviewState state, int * hist, int * hist_view, int size) {

	double path_current_x[size];
	double path_current_y[size];
	double path_current_z[size];

	// calculate lorenz trajectory
	for (int iter = 0; iter < size; iter++) {
		double xt = state->x + state->t * state->a * (state->y - state->x);
		double yt = state->y + state->t * (state->x * (state->b - state->z) - state->y);
		double zt = state->z + state->t * (state->x * state->y - state->c * state->z);
		state->x = xt;
		state->y = yt;
		state->z = zt;
		path_current_x[iter] = state->x;
		path_current_y[iter] = state->y;
		path_current_z[iter] = state->z;
	}

	int path_index;
	// Red channel
	// now shift x,y to be in the range 128/128
	for (int col_idx = 0; col_idx < size; col_idx++) {
		for (int row_idx = 0; row_idx < size; row_idx++) {
			int linindex = row_idx * size + col_idx; //we have to increase column as fastest idx, then row in order to have a zs compatible system
			if(hist[linindex] > state->threshold){
				for (int iter = 0; iter < size/3; iter++) {
					linindex = row_idx * size + col_idx;
					path_index = ((int) round(path_current_x[iter]+row_idx) * size +
									(int) round(path_current_y[iter]+col_idx));
					if( (path_index < (CAMERA_X*CAMERA_Y)) && (path_index >0)){
						hist_view[path_index] = hist_view[path_index]+10;
					}
				}
			}

		}
	}

	// Green channel
	// now shift x,y to be in the range 128/128
	for (int col_idx = 0; col_idx < size; col_idx++) {
		for (int row_idx = 0; row_idx < size; row_idx++) {
			int linindex = row_idx * size + col_idx; //we have to increase column as fastest idx, then row in order to have a zs compatible system
			if(hist[linindex] > state->threshold){
				for (int iter = size/3; iter < size/2; iter++) {
					linindex = row_idx * size + col_idx;
					path_index = ((int) round(path_current_x[iter]+row_idx) * size +
									(int) round(path_current_y[iter]+col_idx));
					if( (path_index < (CAMERA_X*CAMERA_Y)) && (path_index >0)){
						hist_view[(size*size)+path_index] = hist_view[(size*size)+path_index]+10;
					}
				}
			}

		}
	}

	// Blue channel
	// now shift x,y to be in the range 128/128
	for (int col_idx = 0; col_idx < size; col_idx++) {
		for (int row_idx = 0; row_idx < size; row_idx++) {
			int linindex = row_idx * size + col_idx; //we have to increase column as fastest idx, then row in order to have a zs compatible system
			if(hist[linindex] > state->threshold){
				for (int iter = size/2; iter < size; iter++) {
					linindex = row_idx * size + col_idx;
					path_index = ((int) round(path_current_x[iter]+row_idx) * size +
									(int) round(path_current_y[iter]+col_idx));
					if( (path_index < (CAMERA_X*CAMERA_Y)) && (path_index >0)){
						hist_view[(size*size*2)+path_index] = hist_view[(size*size*2)+path_index]+10;
					}
				}
			}

		}
	}

	return (true);
}

static void caerSpiralViewRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).
	caerPolarityEventPacket polarity = va_arg(args, caerPolarityEventPacket);
	int CLASSIFY_IMG_SIZE = va_arg(args, int);
	int * hist = va_arg(args, int*);
	int * packet_hist_view = va_arg(args, int*);
	bool * havespiral = va_arg(args, bool*);
	havespiral[0] = true;

	//update module state
	spiralviewState state = moduleData->moduleState;

	if (!add_spiral_image_map(state, hist, packet_hist_view, CLASSIFY_IMG_SIZE)) {
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to add spirals");
		return;
	};

	caerSpiralViewConfig(moduleData);

}

void caerSpiralViewAddText(uint16_t moduleID, int * hist_packet, caerFrameEventPacket *spiralviewFrame, int size) {

	caerModuleData moduleData = caerMainloopFindModule(moduleID, "SpiralView", CAER_MODULE_PROCESSOR);

	// put info into frame
	if (*spiralviewFrame != NULL) {
		caerFrameEvent singleplot = caerFrameEventPacketGetEvent(*spiralviewFrame, 0);

		uint32_t counter = 0;
		for (size_t x = 0; x < size; x++) {
			for (size_t y = 0; y < size; y++) {
				int linindex = x * size + y;
				singleplot->pixels[counter] = (uint16_t)((int) (hist_packet[linindex]) * 255); // red
				singleplot->pixels[counter + 1] = (uint16_t)((int) (hist_packet[linindex]) * 255); // green
				singleplot->pixels[counter + 2] = (uint16_t)((int) (hist_packet[linindex]) * 255); // blue
				counter += 3;
			}
		}
		//add info to the frame
		caerFrameEventSetLengthXLengthYChannelNumber(singleplot, size, size, 3, *spiralviewFrame);
	}

}

void caerSpiralViewMakeFrame(uint16_t moduleID, int * hist_packet, caerFrameEventPacket *spiralviewFrame, int size) {

	caerModuleData moduleData = caerMainloopFindModule(moduleID, "SpiralView", CAER_MODULE_PROCESSOR);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) { //to do for visualizer change name of field to a more generic one
		sshsNodePutShort(sourceInfoNode, "dataSizeX", size);
		sshsNodePutShort(sourceInfoNode, "dataSizeY", size);
	}

	// put info into frame
	*spiralviewFrame = caerFrameEventPacketAllocate(1, I16T(moduleData->moduleID), 0, size, size, 3);
	caerMainloopFreeAfterLoop(&free, *spiralviewFrame);
	if (*spiralviewFrame != NULL) {
		caerFrameEvent singleplot = caerFrameEventPacketGetEvent(*spiralviewFrame, 0);

		uint32_t counter = 0;
		for (size_t x = 0; x < size; x++) {
			for (size_t y = 0; y < size; y++) {
				int linindex = x * size + y;
				singleplot->pixels[counter] = (uint16_t)((int) (hist_packet[linindex]) * 255); // red
				//singleplot->pixels[counter + 1] = (uint16_t)((int) (hist_packet[linindex]) ); // green
				//singleplot->pixels[counter + 2] = (uint16_t)((int) (hist_packet[linindex]) ); // blue
				counter += 3;
			}
		}
		counter = 0;
		for (size_t x = size; x < size*2; x++) {
			for (size_t y = size; y < size*2; y++) {
				int linindex = x * size + y;
				//singleplot->pixels[counter] = (uint16_t)((int) (hist_packet[linindex]) ); // red
				singleplot->pixels[counter + 1] = (uint16_t)((int) (hist_packet[linindex]) * 255); // green
				//singleplot->pixels[counter + 2] = (uint16_t)((int) (hist_packet[linindex]) ); // blue
				counter += 3;
			}
		}
		counter = 0;
		for(size_t x = size*size*2; x < size*size*3; x++) {
			singleplot->pixels[counter + 2] = (uint16_t)((int) (hist_packet[x]) * 255); // green
			counter += 3;
		}

		//add info to the frame
		caerFrameEventSetLengthXLengthYChannelNumber(singleplot, size, size, 3, *spiralviewFrame);
		//validate frame
		caerFrameEventValidate(singleplot, *spiralviewFrame);
	}

}
