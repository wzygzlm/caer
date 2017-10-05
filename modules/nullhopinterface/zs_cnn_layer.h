#ifndef __ZS_CNN_LAYER_H__
#define __ZS_CNN_LAYER_H__

#include "zs_top_level_pkg.cpp"
#include <vector>
#include "cstdint"
#include "inttypes.h"
#include <stdio.h>
#include "zs_axi_formatter.h"
class zs_cnn_layer {

    public:
        zs_cnn_layer(int layer_idx, int compression_enabled, int kernel_size, int num_input_channels, int num_input_column,
                int num_output_channels, int pooling_enabled, int relu_enabled, int padding, int num_weight, int num_biases);

        zs_cnn_layer(int layer_idx, FILE* net_file);

        zs_cnn_layer();

        int get_num_pass();
        int get_uncompressed_input_image_num_pixels();
        int get_pixels_per_row();
        int get_input_num_rows();
        int get_cnn_stride();
        void print();
        std::vector<uint64_t> *get_load_array(int pass_idx);

        void set_image_in_memory_for_pass(int pass_idx, bool multipass_image_in_memory);
        // layer properties
        int num_output_columns;
        int num_output_rows;
        int num_output_channels;
        // derived config
        int num_sm_output_rows;
        int macs_per_channel;

        int num_sm_per_channel_per_pass;
        int num_sm_output;
        int pooling_enabled;
        int pre_sm_counter_max;
        int uncompressed_input_size;
    private:
        int config_image_in_memory_word_pos_in_load_array;
        zs_axi_formatter class_axi_formatter;
        // layer properties
        int compression_enabled;
        int kernel_side;
        int num_input_channels;
        int num_input_columns;
        int num_input_rows;

        int relu_enabled;
        int padding;
        int cnn_stride;

        int num_weight;
        int num_biases;

        // derived config
        int contiguous_kernels;
        int channel_decode_jump_mask;
        int num_pixel_output_row;

        int effective_num_input_channels;
        int effective_num_output_channels;
        int output_channel_offset;

        // computational parameters

        int num_pass;
        int layer_idx;
        int weight_per_pass;
        int bias_per_pass;
        static const int NUM_MACS = zs_parameters::NUM_MACS;

        // Array to be sent to MACS
        std::vector<std::vector<uint64_t>> load_array;

        void set_layer_idx(int l_layer_idx);
        void set_compression_enabled(int l_compression_enabled);
        void set_kernel_size(int l_kernel_size);
        void set_num_input_channels(int l_num_input_channels);
        void set_num_input_rows(int l_num_input_rows);
        void set_num_input_columns(int l_num_input_column);
        void set_num_output_channels(int l_num_output_channels);
        void set_pooling_enabled(int l_pooling_enabled);
        void set_relu_enabled(int l_relu_enabled);
        void set_padding(int l_padding);
        void set_num_weight(int l_num_weight);
        void set_num_biases(int l_num_biases);
        void set_derived_config();
        void set_layer_config(FILE*l_net_file, int l_layer_idx);
        void generate_config_array();
        void append_weight(int l_weight);
        void append_bias(int l_bias);
        void initialize_layer(int l_layer_idx, int l_compression_enabled, int l_kernel_size, int l_num_input_channels,
                int l_num_input_columns, int l_num_input_rows, int l_num_output_channels, int l_pooling_enabled,
                int l_relu_enabled, int l_padding, int l_num_weight, int l_num_biases, int l_cnn_stride);
        bool read_layer_from_file(FILE* l_net_file, int l_layer_idx);
        std::vector<std::vector<uint64_t>> get_config_array();
        std::vector<std::vector<uint64_t>> get_weight_array(FILE* l_net_file);
        std::vector<std::vector<uint64_t>> get_biases_array(FILE* l_net_file);

};

#endif
