/*
 * zs_fpga_test_top.cpp
 *
 *  Created on: Nov 14, 2016
 *      Author: asa
 */

#define FPGA_MODE
//#define ENABLE_LOG
//#define VERBOSITY_DEBUG
//#define ENABLE_RESULT_MONITOR

#include "zs_driver.hpp"
#include "npp_log_utilities.cpp"
#include <time.h>
#include <sys/time.h>
#include <ctime>
#include <chrono>
#include <string>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include "npp_std_func_pkg.cpp"
#include "roshambo_leds.hpp"

void get_event_type_images(const uint32_t num_frames, const uint32_t test_num_events, const uint32_t num_pixels,
        const bool normalize_images, const int32_t normalization_max, std::vector<std::vector<int16_t>> &images) {

    images.resize(num_frames);
    log_utilities::full("Image type is event-like\n");
    for (unsigned int frame_idx = 0; frame_idx < num_frames; frame_idx++) {
        std::vector<int16_t> input_image(num_pixels);

        //create event like image
        unsigned int num_events = 0;
        while (num_events < test_num_events) {
            unsigned int position = rand() % input_image.size();
            input_image[position]++;
            num_events++;
        }

        if (normalize_images == true) {
            //normalize it - it is not the actual normalization implemented in CAER - but we are not looking for generating "meaningful" images, just "computationally realistic"
            int max = std::max_element(input_image.begin(), input_image.end())[0];

            for (int entry_idx = 0; entry_idx < input_image.size(); entry_idx++) {
                input_image[entry_idx] = (input_image[entry_idx] * normalization_max) / max;

            }
        }
        images[frame_idx] = input_image;
    }
}

void get_png_type_images(const uint32_t num_frames, const uint32_t num_pixels, const bool normalize_images,
        const int32_t normalization_max, const bool constant_pixel, int32_t constant_value, const uint32_t gen_image_max,
        std::vector<std::vector<int16_t>> &images) {

    log_utilities::full("Image type is png\n");

    for (unsigned int frame_idx = 0; frame_idx < num_frames; frame_idx++) {
        std::vector<int16_t> input_image(num_pixels);

        if (constant_pixel == false) {
            for (unsigned int pixel_idx = 0; pixel_idx < num_pixels; pixel_idx++) {
                input_image[pixel_idx] = rand() % gen_image_max;

            }
        } else {

            for (unsigned int pixel_idx = 0; pixel_idx < num_pixels; pixel_idx++) {
                input_image[pixel_idx] = constant_value;
            }

        }

        if (normalize_images == true) {
            //normalize it - it is not the actual normalization implemented in CAER - but we are not looking for generating "meaningful" images, just "computationally realistic"
            int max = std::max_element(input_image.begin(), input_image.end())[0];

            for (int entry_idx = 0; entry_idx < input_image.size(); entry_idx++) {
                input_image[entry_idx] = (input_image[entry_idx] * normalization_max) / max;
            }
        }
        images[frame_idx] = input_image;

    }
}

void get_image_from_file(std::string image_file, const uint32_t num_pixels, std::vector<int16_t> &image) {

    /*uint16_t index = image_file.find_last_of("/");
    if (index != std::string::npos) {
        image_file.replace(0, index + 1, "");
    }*/
    log_utilities::debug("Reading image %s", image_file.c_str());

    FILE *l_net_file = fopen(image_file.c_str(), "r");
    image.resize(num_pixels);

    if (l_net_file == NULL) {
        log_utilities::error("Image file %s opening failed", image_file.c_str());
        throw std::invalid_argument("Failed attempt to image, impossible to proceed");

    } else {
        for (uint32_t pixel_idx = 0; pixel_idx < num_pixels; pixel_idx++) {
            image[pixel_idx] = npp_std::read_int_from_file(l_net_file);
        }
    }
    fclose(l_net_file);
}

void get_image_from_filelist(const std::string filelist, const int32_t num_frames, const uint32_t num_pixels,
        std::vector<std::vector<int16_t>> &images) {

    std::ifstream filelist_file(filelist.c_str());
    uint16_t image_counter = 0;

    while (filelist_file.eof() == false && num_frames > image_counter) {
        image_counter++;
        images.resize(image_counter);

        std::string image_file;
        std::string classification;
        std::getline(filelist_file, image_file);
        std::getline(filelist_file, classification);

        try {
            get_image_from_file(image_file, num_pixels, images[image_counter - 1]);
        } catch (int e) {
            log_utilities::error("Invalid image in list, ending image read");
            return;
        }
    }
}

int main() {

    log_utilities::none("Starting fpga testing...");
    //test mapping:
    //0 = faceNet
    //1 = roshamboNet
    //2 = VGG16_LP16
    //3 = gigaNet
    const uint8_t test = 1;
    const uint16_t num_frames = 100; //Num of frames in both random modes and filelist

    std::string network_file = "";
    std::string image_file = "";
    std::string imagelist_file = "val_images_list.txt";

    uint32_t num_row;
    uint32_t num_column;
    uint32_t num_channels;
    uint32_t test_num_events;
    int32_t normalization_max = 255;
    int32_t gen_image_max = 255;
    int32_t constant_value = 128;

    bool normalize_images = false;
    bool constant_pixel = false;

    bool event_type_image = false;
    bool png_type_image = false;
    bool read_image_from_file = false;
    bool read_image_from_filelist = true;
    switch (test) {
        case (0):
            network_file = "faceNet.nhp";
            num_row = 36;
            num_column = 36;
            num_channels = 1;
            test_num_events = 2000;
            normalize_images = true;
            event_type_image = true;
            break;
        case (1):
            network_file = "roshamboNet.nhp";
            num_row = 64;
            num_column = 64;
            num_channels = 1;
            test_num_events = 2000;
            normalize_images = true;
            event_type_image = true;
            break;
        case (2):
            network_file = "VGG16_LP16.nhp";
            num_row = 224;
            num_column = 224;
            num_channels = 3;
            png_type_image = true;
            break;
        case (3):
            network_file = "gigaNet.nhp";
            num_row = 224;
            num_column = 224;
            num_channels = 3;
            png_type_image = true;
            break;
        default:
            log_utilities::error("Illegal test selected");
            exit(EXIT_FAILURE);
    }

    Roshambo_leds leds;

    log_utilities::none("Starting data preparation...");
    srand(1);
    zs_driver driver(network_file);
    const uint32_t num_pixels = num_channels * num_row * num_column;
    log_utilities::full("Num pixels input image (TB): %d", num_pixels);
    std::vector<std::vector<int16_t>> images(num_frames);

    if (read_image_from_file) {
        get_image_from_file(image_file, num_pixels, images[0]);
    } else if (read_image_from_filelist) {
        get_image_from_filelist(imagelist_file, num_frames, num_pixels, images);
    } else if (event_type_image) {
        get_event_type_images(num_frames, test_num_events, num_pixels, normalize_images, normalization_max, images);
    } else if (png_type_image) {
        get_png_type_images(num_frames, num_pixels, normalize_images, normalization_max, constant_pixel, constant_value,
                gen_image_max, images);
    } else {
        log_utilities::error("No type for image generation chosen!");
        exit(EXIT_FAILURE);
    }

    log_utilities::none("Data preparation done, starting run...\n\n");

    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

    for (size_t frame_idx = 0; frame_idx < num_frames; frame_idx++) {
        int result = driver.classify_image(images[frame_idx].data());
        leds.represent_classification_result(result);
    }

    leds.represent_classification_result(-1); //Unknown result

    std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    duration = duration/1000; //move to milliseconds
    const double duration_avg_ms = duration / (num_frames);

    log_utilities::none("Total time: %f ms - Average over %d frames: %f ms", duration, num_frames, duration_avg_ms);

    usleep(100000);
    return (0);
}

