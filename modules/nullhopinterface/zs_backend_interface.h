#ifndef __ZS_BACKEND_INTERFACE_H__
#define __ZS_BACKEND_INTERFACE_H__



#include "stdio.h"
#include "iostream"
#include "string.h"
#include <vector>
#include "cstdint"
#include "inttypes.h"

#ifdef FPGA_MODE
#include "zs_axi_dma_lib.hpp"
#endif

class zs_backend_interface {

public:
    zs_backend_interface();

    bool write(const std::vector<uint64_t> *array);
    void read(std::vector<uint64_t> &read_array);

    void print_sw_to_zs_words(std::vector<uint64_t> array);

    void print_zs_to_sw_words(std::vector<uint64_t> array);

    void print_axi_words(std::vector<uint64_t> array, FILE* file);

#ifdef RTL_MODE
    void append_new_rtl_word(uint64_t new_word);
#endif

private:

#ifdef FPGA_MODE
    ZS_axidma axi_interface;
#endif

#ifdef SW_TO_ZS_WORDS_LOG
    FILE* sw_to_zs_words_file;
#endif

#ifdef ZS_TO_SW_WORDS_LOG
    FILE* zs_to_sw_words_file;
#endif

#ifdef RTL_MODE
    bool new_rtl_read_word_available;
    uint64_t rtl_read_word;
#endif
};

#endif
