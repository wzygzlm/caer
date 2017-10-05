/*
 * zs_driver.h
 *
 *  Created on: Oct 10, 2016
 *      Author: asa
 */

#include "zs_backend_interface.h"
#include "zs_axi_formatter.h"
#include "zs_cnn_layer.h"
#include "zs_fc_layer.h"
#include "zs_monitor.h"

#include "stdio.h"
#include "iostream"
#include "string.h"
#include <vector>

#include <libcaer/events/frame.h>

#include "npp_performance_profiler.hpp"

#include "zs_top_level_pkg.cpp"

class zs_driver {

    public:
        zs_driver() {
        }

        zs_driver(std::string network_file_name);

        int classify_image(caerFrameEventPacketConst l_image);
        //int classify_image(const int16_t* l_image);
        zs_backend_interface backend_if;
    private:

        zs_axi_formatter pixel_formatter;
        zs_monitor monitor;

        Npp_performance_profiler* performance_profiler;
        uint16_t perf_input_image_conversion, perf_fc_layers, perf_fc_decompression, perf_frame_total_time, perf_conv_layers, perf_network_loading;

        bool class_initialized;
        int total_num_processed_images;
        int num_cnn_layers;
        int num_fc_layers;
        int total_num_layers;

        int first_layer_pixels_per_row;
        int first_layer_num_rows;
        int first_layer_num_pixels;
        int first_layer_num_axi_words;
        bool first_layer_pixels_per_row_odd;

        std::vector<std::vector<uint64_t>> activations;
        std::vector<std::vector<int16_t>> fc_activations;
        std::vector<uint32_t> first_layer_row_start_positions;
        std::vector<uint8_t> first_layer_row_start_positions_word_idx;

        std::vector<zs_cnn_layer> cnn_network;
        std::vector<zs_fc_layer> fc_network;

        inline void convert_input_image_int_to_short(const int* l_image, const uint16_t l_num_row, const uint32_t l_total_num_pixel);
        inline void convert_input_image_short_to_short(const int16_t* l_image, const uint16_t l_num_row,
                const uint32_t l_total_num_pixel);

        inline void compute_fc_layer(const std::vector<int16_t> &l_input, std::vector<int16_t> &fc_output,
                const uint16_t layer_idx);
        inline uint16_t compute_network();
        inline void compute_cnn_layer(const uint16_t layer_idx);
        inline void compute_cnn_layer_singlepass(const uint16_t layer_idx);
        inline void compute_cnn_layer_multipass(const uint16_t layer_idx, const uint16_t num_pass);
        inline void load_kernel_config_biases_for_next_layer(const uint16_t layer_idx);
        inline void load_config_biases_kernels(const uint16_t layer_idx, const uint16_t pass_idx);
        inline void load_image(const std::vector<uint64_t> &l_input);
        inline bool read_network_from_file(const std::string network_file_name);
        inline bool get_multipass_image_in_memory(const uint16_t layer_idx,const uint16_t num_pass);
        inline uint16_t get_input_activation_size(const uint16_t layer_idx);
};
