#ifndef __ZS_CNN_LAYER__
#define __ZS_CNN_LAYER__

#include "zs_cnn_layer.h"
#include  "npp_log_utilities.cpp"
#include "npp_std_func_pkg.cpp"

#include "zs_axi_formatter.h"
#include "iostream"
#include <stdio.h>
#include "string.h"
#include "cstdint"
#include "inttypes.h"
#include <unistd.h>
#include <math.h>
zs_cnn_layer::zs_cnn_layer(int l_layer_idx, FILE* l_net_file) {

    if (read_layer_from_file(l_net_file, l_layer_idx) == false) {
        throw "CNN layer not initialized properly";
    }

}


zs_cnn_layer::zs_cnn_layer() {

}



int zs_cnn_layer::get_uncompressed_input_image_num_pixels() {
    return (num_input_rows * num_input_columns * num_input_channels);
}

int zs_cnn_layer::get_pixels_per_row() {
    return (num_input_rows * num_input_channels);
}

int zs_cnn_layer::get_input_num_rows() {
    return (num_input_rows);
}

int zs_cnn_layer::get_cnn_stride() {
    return (cnn_stride);
}

//NOTE: If the position in the list of address zs_address_space::config_image_in_memory is changed it should be changed also in
//zs_cnn_layer::set_image_in_memory_for_pass
std::vector<std::vector<uint64_t>> zs_cnn_layer::get_config_array() {
    const int REG_TYPE = zs_parameters::REG_TYPE;
    std::vector<std::vector<uint64_t>> configs;

    for (uint16_t pass_idx = 0; pass_idx < num_pass; pass_idx++) {
        zs_axi_formatter axi_formatter = zs_axi_formatter();
        //axi_formatter.format takes: type,address,value
        log_utilities::debug("Starting preparation of config array for pass %d", pass_idx);

        //Word IDX
        //0
        axi_formatter.append(REG_TYPE, zs_address_space::config_image_compression_enabled, compression_enabled);
        axi_formatter.append(REG_TYPE, zs_address_space::config_pre_sm_counter_max, std::log2(pre_sm_counter_max + 1)); //it is decreased by 1 during reading
        // axi_formatter.append(REG_TYPE, zs_address_space::config_pre_sm_counter_max, pre_sm_counter_max);

        //1
        axi_formatter.append(REG_TYPE, zs_address_space::config_kernel_size, kernel_side);
        axi_formatter.append(REG_TYPE, zs_address_space::config_num_input_channels, num_input_channels);

        //2
        axi_formatter.append(REG_TYPE, zs_address_space::config_num_input_column, num_input_columns);
        axi_formatter.append(REG_TYPE, zs_address_space::config_num_input_rows, num_input_rows);

        //3
        axi_formatter.append(REG_TYPE, zs_address_space::config_num_output_channels, effective_num_output_channels);
        axi_formatter.append(REG_TYPE, zs_address_space::config_pooling_enabled, pooling_enabled);

        //4
        axi_formatter.append(REG_TYPE, zs_address_space::config_relu_enabled, relu_enabled);
        axi_formatter.append(REG_TYPE, zs_address_space::config_contiguous_kernels, contiguous_kernels);

        //5
        // axi_formatter.append(REG_TYPE, zs_address_space::config_num_macs_per_channel, macs_per_channel - 1); //The actual value in hw is from 0 to 7
        axi_formatter.append(REG_TYPE, zs_address_space::config_num_macs_per_channel, std::log2(macs_per_channel)); //The actual value in hw is in power of 2
        axi_formatter.append(REG_TYPE, zs_address_space::config_input_channel_decode_jump_mask, channel_decode_jump_mask);

        //6
        axi_formatter.append_new_word(REG_TYPE, zs_address_space::config_kernel_memory_write_complete_pulse, 0);
        axi_formatter.append_new_word(REG_TYPE, zs_address_space::config_kernel_memory_resetn_pulse, 0);

        //7
        axi_formatter.append(REG_TYPE, zs_address_space::config_row_column_offset, padding);
        axi_formatter.append(REG_TYPE, zs_address_space::config_multipass_idx, pass_idx);

        //8
        //to allow the reshaper to place properly the new row flag, we need to set the number of pixel equivalent to the pixel produced in
        //the first pass
        axi_formatter.append(REG_TYPE, zs_address_space::config_num_pixel_per_output_row, num_pixel_output_row / num_pass);

        axi_formatter.flush_word();

        //9
        //IMPORTANT: IF CHANGED, IT MUST CHANGE ALSO IN zs_cnn_layer::set_image_in_memory_for_pass
        config_image_in_memory_word_pos_in_load_array = axi_formatter.array.size();
        if (pass_idx == 0) {
            axi_formatter.append(REG_TYPE, zs_address_space::config_image_in_memory, 0);
        } else {
            axi_formatter.append(REG_TYPE, zs_address_space::config_image_in_memory, 1);
        }

        axi_formatter.flush_word();

        //10
        //--

        std::vector<uint64_t> pass_config = axi_formatter.get_array();
        configs.push_back(pass_config);

    }
    log_utilities::debug("Config array setup completed");
    return (configs);
}

void zs_cnn_layer::set_image_in_memory_for_pass(int pass_idx, bool multipass_image_in_memory) {

    const uint16_t image_in_memory_flag = multipass_image_in_memory;

    log_utilities::debug("Setting flag with value %d, old word: %llu", multipass_image_in_memory,
            load_array[pass_idx][config_image_in_memory_word_pos_in_load_array]);
//uint64_t zs_axi_formatter::format_word0(uint16_t l_short_value, uint16_t l_utype, uint16_t l_uvalid, uint16_t l_uaddress)
    load_array[pass_idx][config_image_in_memory_word_pos_in_load_array] =
            class_axi_formatter.format_word0(
                    image_in_memory_flag,
                    zs_parameters::REG_TYPE,
                    (uint16_t) 1,
                    zs_address_space::config_image_in_memory);

    log_utilities::debug("New word: %llu", load_array[pass_idx][config_image_in_memory_word_pos_in_load_array]);

}

std::vector<std::vector<uint64_t>> zs_cnn_layer::get_weight_array(FILE* l_net_file) {

    //Read kernels weight
    std::vector<std::vector<uint64_t>> weights;

    unsigned int pos_tmp;
    unsigned int x_ker;
    unsigned int y_ker;
    unsigned int input_ch_ker;
    unsigned int out_ch_ker;
    unsigned int effective_num_weights;
    unsigned int effective_channels_ratio;
    int num_weight_read_from_file = 0;
    uint64_t valid_word_in_weight_array = 0;
    const int total_num_weight_from_file = num_input_channels * num_output_channels * kernel_side * kernel_side;
    int l_weight;
    const int KER_TYPE = zs_parameters::KER_TYPE;
    const int REG_TYPE = zs_parameters::REG_TYPE;

    log_utilities::debug("Preparing weights array...");

    effective_num_weights = effective_num_input_channels * effective_num_output_channels * kernel_side * kernel_side;
    weights.reserve(num_pass);

    for (int pass_idx = 0; pass_idx < num_pass; pass_idx++) {
        zs_axi_formatter axi_formatter = zs_axi_formatter();

        for (int weight_idx = 0; weight_idx < effective_num_weights; weight_idx++) {
            pos_tmp = weight_idx;
            x_ker = pos_tmp % kernel_side; //x dimension of the kernel
            pos_tmp = pos_tmp / kernel_side;
            y_ker = pos_tmp % kernel_side; //y dimension of the kernel
            pos_tmp = pos_tmp / kernel_side;
            input_ch_ker = pos_tmp % effective_num_input_channels;
            pos_tmp = pos_tmp / effective_num_input_channels;
            out_ch_ker = pos_tmp + pass_idx * effective_num_output_channels;

            effective_channels_ratio = effective_num_output_channels / num_output_channels;

            /*fprintf(stderr,"xPos: %d\n",x_ker);
             fprintf(stderr,"yPos: %d\n",y_ker);
             fprintf(stderr,"srcChPos: %d\n",input_ch_ker);
             fprintf(stderr,"m_nchIn_pseudo: %d\n",effective_num_input_channels);
             fprintf(stderr,"dstChPos: %d\n",out_ch_ker);
             fprintf(stderr,"pseudo_ratio: %d\n",effective_channels_ratio);*/

            if (
            //! (out_ch_ker % effective_channels_ratio) && //This piece of code is nonsense in original hesham driver
            input_ch_ker < num_input_channels) {
                // fprintf(stderr,"VALUE\n");
                l_weight = npp_std::read_int_from_file(l_net_file);
                num_weight_read_from_file++;
                axi_formatter.append(KER_TYPE, 0, l_weight);
                valid_word_in_weight_array++;
            } else {
                // fprintf(stderr,"**ZERO\n");
                axi_formatter.append(KER_TYPE, 0, 0);
                valid_word_in_weight_array++;
            }

            if ( (weight_idx + 1) % contiguous_kernels == 0)
                //  fprintf(stderr,"****break\n");
                axi_formatter.flush_word();

        }
        axi_formatter.append_new_word(REG_TYPE, zs_address_space::config_kernel_memory_write_complete_pulse, 1);

        //We can start the processing only when kernels are loaded
        axi_formatter.append_new_word(REG_TYPE, zs_address_space::config_start_process_pulse, 1);

        //XXX We need to append an empty word because of AXI fifo being not properly implemented
        //Sometimes a word gets stuck into the fifo, appending this empty word we ensure it is flush properly
        //Can be removed once mm2s2zs from Seville is fixed - since it is only 1 word it is computationally irrelevant
        axi_formatter.append_empty();

        weights.push_back(axi_formatter.get_array());


    }

    // throw std::exception();
    if (num_weight_read_from_file != total_num_weight_from_file) {
        log_utilities::error("Wrong number of weight read from network file: %d,expected %d", num_weight_read_from_file,
                total_num_weight_from_file);
        throw "Wrong number of weight read from network file";
    }

    log_utilities::full("Size of weights to be stored in a single pass: %d KB", ((valid_word_in_weight_array/num_pass)*2)/1024);
    log_utilities::debug("Weights array ready");
    return (weights);

}

std::vector<std::vector<uint64_t>> zs_cnn_layer::get_biases_array(FILE* l_net_file) {

    std::vector<std::vector<uint64_t>> biases;
    int l_bias;
    const int BIAS_TYPE = zs_parameters::BIAS_TYPE;
    log_utilities::debug("Preparing biases array...");
    biases.reserve(num_pass);
    for (int pass_idx = 0; pass_idx < num_pass; pass_idx++) {

        zs_axi_formatter axi_formatter = zs_axi_formatter();
        for (int bias_idx = 0; bias_idx < bias_per_pass; bias_idx++) {

            if (bias_idx % macs_per_channel == 0) {
                l_bias = npp_std::read_int_from_file(l_net_file);
                axi_formatter.append(BIAS_TYPE, bias_idx, l_bias);
            } else {
                axi_formatter.append(BIAS_TYPE, bias_idx, 0);
            }

        }

        std::vector<uint64_t> biases_pass_array = axi_formatter.get_array();

        if (biases_pass_array.size()*2 != NUM_MACS) {
            log_utilities::error("Bias preparation exception, bias size: %d, NUM_MACS: %d", biases_pass_array.size(), NUM_MACS);
            throw "Inconsistent number of bias to load";
        } else {
            log_utilities::debug("Biases array layer %d - pass %d consistency check passed", layer_idx, pass_idx);
        }

        biases.push_back(biases_pass_array);
    }

    if (biases.size() != num_pass) {
        throw "Inconsistent number of bias to load once final array merged";
    } else {
        log_utilities::debug("Biases array layer %d consistency check passed", layer_idx);
    }

    log_utilities::debug("Biases array ready");
    return (biases);
}

//This function is mostly based on Hesham original driver
void zs_cnn_layer::set_derived_config() {

    int kernel_memories_required;
    int macs_per_channel_required;
    int kernel_side_square;
    int single_pass_output_channels;

    log_utilities::debug("Computing derived configuration parameters...");
    kernel_side_square = kernel_side * kernel_side;
    kernel_memories_required = kernel_side_square * num_input_channels / (4096) + 1;

    if (num_output_channels > NUM_MACS) {
        int num_pass_rounded = num_output_channels / NUM_MACS;
        single_pass_output_channels = num_output_channels / (num_pass_rounded + (num_output_channels % NUM_MACS ? 1 : 0));
        macs_per_channel_required = NUM_MACS / single_pass_output_channels;
    } else {
        macs_per_channel_required = NUM_MACS / num_output_channels;
    }

    //With this IF we decide if the num of active macs in 1 pass is due to upper limit in terms of memory or for the upper limit
    //in terms of num of macs
    if (macs_per_channel_required > kernel_memories_required) {
        macs_per_channel = macs_per_channel_required;
    } else {
        macs_per_channel = kernel_memories_required;
    }

    //Only even num of macs per channel supported, so we round up
    if (macs_per_channel == 1)
        macs_per_channel = 1;
    else if (macs_per_channel <= 2)
        macs_per_channel = 2;
    else if (macs_per_channel <= 4)
        macs_per_channel = 4;
    else if (macs_per_channel <= 8)
        macs_per_channel = 8;
    else {
        fprintf(stderr, "Invalid macs_per_channel %d\n", macs_per_channel);
    }

    effective_num_output_channels = (NUM_MACS / macs_per_channel);
    num_pass = num_output_channels / effective_num_output_channels;

    int nearest_pow2_input_channels = 1;
    while (nearest_pow2_input_channels < num_input_channels) {
        nearest_pow2_input_channels *= 2;
    }

    if (macs_per_channel == 1) {
        contiguous_kernels = num_input_channels * kernel_side_square;
        channel_decode_jump_mask = nearest_pow2_input_channels - 1;
        effective_num_input_channels = num_input_channels;
    } else {
        int num_dummy_kernels;
        num_dummy_kernels = (nearest_pow2_input_channels - num_input_channels) * kernel_side_square;

        if (nearest_pow2_input_channels <= macs_per_channel) {
            contiguous_kernels = kernel_side_square;
            num_dummy_kernels += (macs_per_channel - nearest_pow2_input_channels) * kernel_side_square;
        } else {
            contiguous_kernels = (nearest_pow2_input_channels / macs_per_channel) * kernel_side_square;
        }

        effective_num_input_channels = num_input_channels + (num_dummy_kernels / kernel_side_square);
        channel_decode_jump_mask = (contiguous_kernels / kernel_side_square) - 1;

    }

    weight_per_pass = num_weight / num_pass;
    bias_per_pass = NUM_MACS;
    pre_sm_counter_max = (NUM_MACS / (zs_parameters::NUM_MACS_PER_CLUSTER * macs_per_channel)) - 1;

    num_output_columns = (num_input_columns - kernel_side + 1 + padding * 2) / (pooling_enabled + 1);
    num_output_rows = (num_input_rows - kernel_side + 1 + padding * 2) / (pooling_enabled + 1);
    num_pixel_output_row = num_output_columns * num_output_channels;
    num_sm_output_rows = (num_output_columns * num_output_channels) / zs_parameters::SPARSITY_MAP_WORD_NUM_BITS;
    num_sm_per_channel_per_pass = effective_num_output_channels / zs_parameters::SPARSITY_MAP_WORD_NUM_BITS;
    num_sm_output = (num_output_rows * num_output_columns * num_output_channels) / zs_parameters::SPARSITY_MAP_WORD_NUM_BITS;
    uncompressed_input_size = num_input_rows*num_input_columns*num_input_channels;
    log_utilities::full("Derived configuration parameters computation done");

}



void zs_cnn_layer::initialize_layer(int l_layer_idx, int l_compression_enabled, int l_kernel_size, int l_num_input_channels,
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

    set_derived_config();

    if (padding > 0 && num_output_rows % 2 != 0) {
        log_utilities::medium("Odd number of output rows with padding enabled - HW currently not debugged for this operation mode");
    }

}
void zs_cnn_layer::set_layer_config(FILE*l_net_file, int l_layer_idx) {

    int l_compression_enabled, l_kernel_size, l_num_input_channels, l_num_input_columns, l_num_input_rows, l_num_output_channels,
            l_pooling_enabled, l_relu_enabled, l_padding, l_bias, l_num_biases, l_num_weight, l_cnn_stride;

    log_utilities::full("Reading layer parameters...");

    //Read layer config
    l_compression_enabled = npp_std::read_int_from_file(l_net_file);
    l_kernel_size = npp_std::read_int_from_file(l_net_file);
    l_num_input_channels = npp_std::read_int_from_file(l_net_file);
    l_num_input_columns = npp_std::read_int_from_file(l_net_file);
    l_num_input_rows = npp_std::read_int_from_file(l_net_file);
    l_num_output_channels = npp_std::read_int_from_file(l_net_file);
    l_pooling_enabled = npp_std::read_int_from_file(l_net_file);
    l_relu_enabled = npp_std::read_int_from_file(l_net_file);
    l_padding = npp_std::read_int_from_file(l_net_file);
    l_cnn_stride = npp_std::read_int_from_file(l_net_file);
    l_num_weight = l_kernel_size * l_kernel_size * l_num_input_channels * l_num_output_channels;
    l_num_biases = l_num_output_channels;

    //Layer initialization
    initialize_layer(l_layer_idx, l_compression_enabled, l_kernel_size, l_num_input_channels, l_num_input_columns,
            l_num_input_rows, l_num_output_channels, l_pooling_enabled, l_relu_enabled, l_padding, l_num_weight, l_num_biases,
            l_cnn_stride);

    log_utilities::full("Layer config setting completed");
}

bool zs_cnn_layer::read_layer_from_file(FILE* l_net_file, int l_layer_idx) {

    set_layer_config(l_net_file, l_layer_idx);

    std::vector<std::vector<uint64_t>> config_array = get_config_array();
    std::vector<std::vector<uint64_t>> weight_array = get_weight_array(l_net_file);
    std::vector<std::vector<uint64_t>> biases_array = get_biases_array(l_net_file);

    log_utilities::full("Merging config, weights and biases into loading-ready arrays...");
    load_array.resize(num_pass);
    for (int pass_idx = 0; pass_idx < num_pass; pass_idx++) {

        load_array[pass_idx].reserve(
                config_array[pass_idx].size() + biases_array[pass_idx].size() + weight_array[pass_idx].size());

        //Config is inserted at the beginning for both HW and SW reasons. Dont move.
        load_array[pass_idx].insert(load_array[pass_idx].begin(), config_array[pass_idx].begin(), config_array[pass_idx].end());
        load_array[pass_idx].insert(load_array[pass_idx].end(), biases_array[pass_idx].begin(), biases_array[pass_idx].end());
        load_array[pass_idx].insert(load_array[pass_idx].end(), weight_array[pass_idx].begin(), weight_array[pass_idx].end());

    }

    print();
    log_utilities::full("Layer read from file completed");
    return (true);

}

int zs_cnn_layer::get_num_pass() {
    return (num_pass);
}

std::vector<uint64_t>* zs_cnn_layer::get_load_array(int pass_idx) {
    return ( &load_array[pass_idx]);
}

void zs_cnn_layer::print() {
    log_utilities::full("Convolutional Layer %d report:", layer_idx);

    log_utilities::full("num_input_channels: %d", num_input_channels);
    log_utilities::full("num_input_columns: %d", num_input_columns);
    log_utilities::full("num_input_rows: %d", num_input_rows);
    log_utilities::full("num_output_channels: %d", num_output_channels);
    log_utilities::full("num_output_columns: %d", num_output_columns);
    log_utilities::full("num_output_rows: %d", num_output_rows);
    log_utilities::full("kernel_size: %d", kernel_side);

    log_utilities::full("compression_enabled: %d", compression_enabled);
    log_utilities::full("pooling_enabled: %d", pooling_enabled);
    log_utilities::full("relu_enabled: %d", relu_enabled);
    log_utilities::full("padding: %d", padding);
    log_utilities::full("cnn_stride: %d", cnn_stride);

    log_utilities::full("pre_sm_counter_max: %d", pre_sm_counter_max);
    log_utilities::full("macs_per_channel: %d", macs_per_channel);
    log_utilities::full("contiguous_kernels: %d", contiguous_kernels);
    log_utilities::full("channel_decode_jump_mask: %d", channel_decode_jump_mask);

    log_utilities::full("num_pass: %d", num_pass);
    log_utilities::full("num_weight: %d", num_weight);
    log_utilities::full("num_biases: %d", num_biases);
    log_utilities::full("weight_per_pass: %d", weight_per_pass);
    log_utilities::full("bias_per_pass: %d", bias_per_pass);

    log_utilities::full("effective_num_input_channels: %d", effective_num_input_channels);
    log_utilities::full("effective_num_output_channels: %d", effective_num_output_channels);
    log_utilities::full("num_sm_output_rows: %d", num_sm_output_rows);
    log_utilities::full("num_sm_per_channel_per_pass: %d", num_sm_per_channel_per_pass);
    log_utilities::full("num_sm_output: %d", num_sm_output);

}

#endif
