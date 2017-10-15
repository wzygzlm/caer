/*
 * zs_top_level_sw_pkg.cpp
 *
 *  Created on: Nov 1, 2016
 *      Author: asa
 */
#ifndef __ZS_TOP_LEVEL_PKG__
#define __ZS_TOP_LEVEL_PKG__
#include "stdio.h"
#include "string.h"
#include "cstdint"
#include "inttypes.h"
#include <cmath>

namespace zs_parameters {
    static const uint16_t IDP_MEMORY_SIZE_KB = 512;

    static const uint8_t ACTIVATIONS_NUM_BITS = 16;
    static const uint8_t MANTISSA_NUM_BITS = 8;
    static const uint16_t MANTISSA_RESCALE_FACTOR = std::pow(2, zs_parameters::MANTISSA_NUM_BITS);
    static const uint16_t ACTIVATION_RESCALE_FACTOR = std::pow(2, zs_parameters::ACTIVATIONS_NUM_BITS);

    static const uint16_t NUM_MACS = 128;
    static const uint8_t NUM_MACS_PER_CLUSTER = 16;

    static const uint8_t REG_TYPE = 3;
    static const uint8_t KER_TYPE = 2;
    static const uint8_t IMG_TYPE = 1;
    static const uint8_t BIAS_TYPE = 0;

    static const uint8_t SPARSITY_MAP_WORD_NUM_BITS = 16;
}

namespace zs_address_space {

    static const uint8_t num_registers = 22;

    static const uint8_t config_image_compression_enabled = 0;
    static const uint8_t config_pre_sm_counter_max = 1;
    static const uint8_t config_kernel_size = 2;
    static const uint8_t config_num_input_channels = 3;
    static const uint8_t config_num_input_column = 4;
    static const uint8_t config_num_input_rows = 5;
    static const uint8_t config_num_output_channels = 6;
    static const uint8_t config_pooling_enabled = 7;
    static const uint8_t config_relu_enabled = 8;
    static const uint8_t config_contiguous_kernels = 9;
    static const uint8_t config_num_macs_per_channel = 10;
    static const uint8_t config_input_channel_decode_jump_mask = 11;
    static const uint8_t config_kernel_memory_write_complete_pulse = 12;
    static const uint8_t config_kernel_memory_resetn_pulse = 13;
    static const uint8_t config_image_load_done_pulse = 14;
    static const uint8_t config_layer_channel_offset = 16;
    static const uint8_t config_first_conv_layer = 17;
    static const uint8_t config_pixel_memory_loop_offset = 18;
    static const uint8_t config_start_process_pulse = 19;
    static const uint8_t config_image_in_memory = 20;
    static const uint8_t config_row_column_offset = 21;
    static const uint8_t config_multipass_idx = 22;
    static const uint8_t config_num_pixel_per_output_row = 23;

    static const uint8_t config_image_start_new_row_instr = 1;

}

namespace zs_axi_bits {

    static const uint8_t NUM_VALUES_INPUT_WORD = 2;

    static const uint8_t ADDRESS_SIZE = 7;
    static const uint8_t TYPE_SIZE = 2;
    static const uint8_t VALID_SIZE = 2;
    static const uint8_t VALUE_SIZE = 16;
    static const uint8_t BURST_SIZE = 13;

    static const uint8_t SECOND_VALUE_SHIFT = 16;
    static const uint8_t TYPE_VALUE_SHIFT = 32;
    static const uint8_t FIRST_VALID_SHIFT = 34;
    static const uint8_t SECOND_VALID_SHIFT = 35;
    static const uint8_t FIRST_ADDR_SHIFT = 36;
    static const uint8_t SECOND_ADDR_SHIFT = 43;
    static const uint8_t READ_TRANSFER_SET_VALID_SHIFT = 63;


//Input Masks
    static const uint64_t FIRST_VALUE_MASK = (uint64_t) (std::pow(2, VALUE_SIZE) - 1);
    static const uint64_t SECOND_VALUE_MASK = (uint64_t) ((uint64_t) (std::pow(2, VALUE_SIZE) - 1) << SECOND_VALUE_SHIFT);
    static const uint64_t TYPE_MASK = (uint64_t) ((uint64_t) (std::pow(2, TYPE_SIZE) - 1) << TYPE_VALUE_SHIFT);
    static const uint64_t VALID_MASK = (uint64_t) ((uint64_t) (std::pow(2, VALID_SIZE) - 1) << FIRST_VALID_SHIFT);
    static const uint64_t FIRST_VALID_MASK = (uint64_t) ((uint64_t) 1) << (FIRST_VALID_SHIFT);
    static const uint64_t SECOND_VALID_MASK = (uint64_t) ((uint64_t) 1) << (SECOND_VALID_SHIFT);
    static const uint64_t FIRST_ADDRESS_MASK = (uint64_t) ((uint64_t) (std::pow(2, ADDRESS_SIZE) - 1) << FIRST_ADDR_SHIFT);
    static const uint64_t SECOND_ADDRESS_MASK = (uint64_t) ((uint64_t) (std::pow(2, ADDRESS_SIZE) - 1) << SECOND_ADDR_SHIFT);


    static const uint64_t ZS_ALL_INPUT_MASK = FIRST_VALUE_MASK | SECOND_VALUE_MASK | TYPE_MASK | VALID_MASK | FIRST_VALID_MASK
            | SECOND_VALID_MASK | FIRST_ADDRESS_MASK | SECOND_ADDRESS_MASK;

}

namespace zs_mem_transfer_params {
    static const uint8_t AXI_WIDTH = 64;
    static const uint64_t ZS_WRITE_TRANSFER_LENGTH_BYTES = 4096*8; //1 KB
    static const uint64_t ZS_WRITE_TRANSFER_LENGTH_WORDS = zs_mem_transfer_params::ZS_WRITE_TRANSFER_LENGTH_BYTES / (zs_mem_transfer_params::AXI_WIDTH / 8);
    static const uint64_t ZS_READ_TRANSFER_LENGTH_BYTES = 0x400; //1 KB
    static const uint64_t ZS_READ_TRANSFER_LENGTH_WORDS = zs_mem_transfer_params::ZS_READ_TRANSFER_LENGTH_BYTES / (zs_mem_transfer_params::AXI_WIDTH / 8);




}

#endif
