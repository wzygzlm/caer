#ifndef __AXI_DMA_PKG_PKG__
#define __AXI_DMA_PKG_PKG__
#include "stdio.h"
#include "string.h"
#include "cstdint"
#include "inttypes.h"
#include <cmath>

namespace axi_parameters {
   // static const uint8_t BURST_SIZE = 61;

//    static const uint64_t BURST_VALUE_MASK = (uint64_t) ((uint64_t) (std::pow(2, BURST_SIZE) - 1));

    static const uint8_t IDLE_SHIFT = 63;
    static const uint64_t IDLE_MASK = (uint64_t) std::pow(2, IDLE_SHIFT);

    static const uint8_t READ_TRANSFER_SET_VALID_SHIFT = 62;
    static const uint8_t READ_TRANSFER_SET_MODE_SHIFT = 61;


    static const uint8_t AXI_WIDTH = 64;
    static const uint64_t DEFAULT_AXI_WRITE_TRANSFER_LENGTH_BYTES = 25 * 1024; //1 KB
    static const uint64_t AXI_WRITE_TRANSFER_LENGTH_WORDS = axi_parameters::DEFAULT_AXI_WRITE_TRANSFER_LENGTH_BYTES
            / (axi_parameters::AXI_WIDTH / 8);


    static const uint64_t DEFAULT_AXI_READ_TRANSFER_LENGTH_BYTES = 1 * 1024; //1 KB
    static const uint64_t AXI_READ_TRANSFER_LENGTH_WORDS = axi_parameters::DEFAULT_AXI_READ_TRANSFER_LENGTH_BYTES
            / (axi_parameters::AXI_WIDTH / 8);

    enum axidma_transfer_mode {partial, completed};
    enum axidma_buffer_mode {single_B, double_B};

}

#endif
