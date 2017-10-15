#ifndef __zs_fc_layer__
#define __zs_fc_layer__

#include "zs_fc_layer.h"
#include  "npp_log_utilities.cpp"
#include "npp_std_func_pkg.cpp"
#include "zs_top_level_pkg.cpp"
#include "stdio.h"
#include "string.h"
#include "cstdint"
#include "inttypes.h"
#include <unistd.h>
#include <vector>

zs_fc_layer::zs_fc_layer(int l_layer_idx, FILE* l_net_file) {

    if (read_layer_from_file(l_net_file, l_layer_idx) == false) {
        throw "FC layer not initialized properly";
    }

}

zs_fc_layer::zs_fc_layer() {


}

int zs_fc_layer::get_input_num_rows() {
    return (num_input_rows);
}


void zs_fc_layer::read_weights(FILE* l_net_file, std::vector<std::vector<int16_t>> &weights) {
    //weights are indexed internally as [kernel_idx][channel][column][row]
    //weights are listed in the file as (fastest changing) column - row  - channel -kernel_idx (slower changing)

    log_utilities::debug("Reading FC layer weights...");

    if (kernel_side > 1) {
        log_utilities::debug("FC Layer input is 3D image");
        std::vector<std::vector<std::vector<std::vector<int16_t>>> >l_weights;

        weights.resize(num_output_channels);
        l_weights.resize(num_output_channels);

        for (size_t kernel_idx = 0; kernel_idx < num_output_channels; kernel_idx++) {
            l_weights[kernel_idx].resize(num_input_channels);

            for (size_t input_channel_idx = 0; input_channel_idx < num_input_channels; input_channel_idx++) {
                l_weights[kernel_idx][input_channel_idx].resize(kernel_side);

                for (size_t row_idx = 0; row_idx < kernel_side; row_idx++) {

                    l_weights[kernel_idx][input_channel_idx][row_idx].resize(kernel_side);

                    for (size_t column_idx = 0; column_idx < kernel_side; column_idx++) {
                        int weight = npp_std::read_int_from_file(l_net_file);

                        l_weights[kernel_idx][input_channel_idx][row_idx][column_idx] = weight;
                        //  log_utilities::debug("Weight 16b: %d - Weight 64b:%lld",weight,weight64bit );
                    }
                }
            }
        }

        log_utilities::debug("Reordering FC layer weights...");

        for (size_t kernel_idx = 0; kernel_idx < num_output_channels; kernel_idx++) {
            for (size_t row_idx = 0; row_idx < kernel_side; row_idx++) {
                for (size_t column_idx = 0; column_idx < kernel_side; column_idx++) {
                    for (size_t channel_idx = 0; channel_idx < num_input_channels; channel_idx++) {
                        weights[kernel_idx].push_back(l_weights[kernel_idx][channel_idx][row_idx][column_idx]);

                    }

                }

            }
            std::vector<std::vector<std::vector<int16_t>>>().swap(l_weights[kernel_idx]);
        }
        std::vector<std::vector<std::vector<std::vector<int16_t>>> >().swap(l_weights);
    } else {
        log_utilities::debug("FC Layer input is 1D vector");
        weights.resize(num_output_channels);
        for(size_t kernel_idx = 0; kernel_idx < num_output_channels; kernel_idx++) {
            weights[kernel_idx].resize(num_input_channels);

            for(size_t input_channel_idx = 0; input_channel_idx < num_input_channels; input_channel_idx++) {
                int weight = npp_std::read_int_from_file(l_net_file);
                weights[kernel_idx][input_channel_idx] = weight;

            }
        }
    }

    log_utilities::debug("FC layer weights read completed");
}

void zs_fc_layer::read_biases(FILE* l_net_file, std::vector<int32_t> &biases) {
    //biases are multiplied by MANTISSA_RESCALE_FACTOR in order to avoid to re shift FC results during live computation in zs_driver

    for (size_t kernel_idx = 0; kernel_idx < num_output_channels; kernel_idx++) {
        biases.push_back(npp_std::read_int_from_file(l_net_file) * zs_parameters::MANTISSA_RESCALE_FACTOR);
    }

}

void zs_fc_layer::initialize_layer(int l_layer_idx, int l_compression_enabled, int l_kernel_size, int l_num_input_channels,
        int l_num_input_columns, int l_num_input_rows, int l_num_output_channels, int l_pooling_enabled, int l_relu_enabled,
        int l_padding, int l_num_weight, int l_num_biases, int l_cnn_stride) {

    layer_idx = l_layer_idx;
    compression_enabled = l_compression_enabled;
    kernel_side = l_kernel_size;
    num_input_channels = l_num_input_channels;
    num_input_columns = l_num_input_columns;
    num_input_rows = l_num_input_rows;

    num_output_channels = l_num_output_channels;
    pooling_enabled = l_pooling_enabled;
    relu_enabled = l_relu_enabled;
    padding = l_padding;
    num_weight = l_num_weight;
    num_biases = l_num_biases;
    cnn_stride = l_cnn_stride;
    uncompressed_input_size = num_input_rows*num_input_columns*num_input_channels;
}

void zs_fc_layer::set_layer_config(FILE*l_net_file, int l_layer_idx) {

    int l_compression_enabled, l_kernel_size, l_num_input_channels, l_num_input_columns, l_num_input_rows, l_num_output_channels,
            l_pooling_enabled, l_relu_enabled, l_padding, l_weight, l_bias, l_num_biases, l_num_weight, l_cnn_stride;

    log_utilities::debug("Reading layer parameters...");

    l_compression_enabled = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("compression_enabled: %d", l_compression_enabled);
    l_kernel_size = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("kernel_size: %d", l_kernel_size);
    l_num_input_channels = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("num_input_channels: %d", l_num_input_channels);
    l_num_input_columns = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("num_input_columns: %d", l_num_input_columns);
    l_num_input_rows = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("num_input_rows: %d", l_num_input_rows);
    l_num_output_channels = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("num_output_channels: %d", l_num_output_channels);
    l_pooling_enabled = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("pooling_enabled: %d", l_pooling_enabled);
    l_relu_enabled = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("relu_enabled: %d", l_relu_enabled);
    l_padding = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("padding: %d", l_padding);
    l_cnn_stride = npp_std::read_int_from_file(l_net_file);
    log_utilities::debug("l_cnn_stride: %d", l_cnn_stride);

    l_num_weight = l_kernel_size * l_kernel_size * l_num_input_channels * l_num_output_channels;
    log_utilities::debug("num_weight: %d", l_num_weight);
    l_num_biases = l_num_output_channels;
    log_utilities::debug("num_biases: %d", l_num_biases);

    //Layer initialization
    initialize_layer(l_layer_idx, l_compression_enabled, l_kernel_size, l_num_input_channels, l_num_input_columns,
            l_num_input_rows, l_num_output_channels, l_pooling_enabled, l_relu_enabled, l_padding, l_num_weight, l_num_biases,
            l_cnn_stride);

    log_utilities::debug("Layer config setting completed");
}

bool zs_fc_layer::read_layer_from_file(FILE* l_net_file, int l_layer_idx) {

    set_layer_config(l_net_file, l_layer_idx);
    read_weights(l_net_file, weights);
    read_biases(l_net_file, biases);

    log_utilities::debug("Layer read from file completed");
    return (true);

}

#endif
