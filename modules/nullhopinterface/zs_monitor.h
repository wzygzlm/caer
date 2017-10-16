/*
 * zs_monitor.h
 *
 *  Created on: Nov 10, 2016
 *      Author: asa
 */

#ifndef ZS_MONITOR_H_
#define ZS_MONITOR_H_

#include "zs_cnn_layer.h"
#include "zs_monitor_cnn_layer.h"
#include <string>

class zs_monitor {
    public:
        zs_monitor(std::string filename);
        zs_monitor();
        void classify_image(const int* image);
        void classify_image(const int16_t* image);
        void check_layer_activations(const std::vector<uint64_t> activations, int layer_idx);
        int get_monitor_classification();
        void check_classification(const int classification_result);
    private:
        uint16_t cnn_num_layers;
        bool read_network_from_file(std::string network_file_name);
        std::vector<zs_monitor_cnn_layer> cnn_kernels;
        std::vector<std::vector<std::vector<std::vector<int64_t>>> >monitor_activations;
        void write_activations_to_file( std::vector<std::vector<std::vector<std::vector<int64_t>>> > l_activations);
        void image_1d_to_3d(const int*l_image, const uint32_t num_rows, const uint32_t num_columns,const uint32_t num_channels, std::vector<std::vector<std::vector<int64_t>>> &new_image);
        void image_1d_to_3d(const int16_t*l_image, const uint32_t num_rows, const uint32_t num_columns,const uint32_t num_channels,std::vector<std::vector<std::vector<int64_t>>> &new_image);

        void compute_network();
        void compute_layer(const std::vector<std::vector<std::vector<int64_t>>> layer_input, const zs_monitor_cnn_layer layer_parameters, std::vector<std::vector<std::vector<int64_t>>> &layer_result);
        void compute_convolution(const std::vector<std::vector<std::vector<int64_t>>> layer_input, const zs_monitor_cnn_layer &layer_parameters, std::vector<std::vector<std::vector<int64_t>>> &output_image);
        void compute_pooling(const std::vector<std::vector<std::vector<int64_t>>> layer_input, std::vector<std::vector<std::vector<int64_t>>> &final_pooling);
    };

#endif /* ZS_MONITOR_H_ */
