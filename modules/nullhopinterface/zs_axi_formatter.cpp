#ifndef __ZS_AXI_FORMATTER_CCP__
#define __ZS_AXI_FORMATTER_CCP__

#include "npp_std_func_pkg.cpp"
#include "zs_axi_formatter.h"
#include "stdio.h"
#include <iostream>
#include <vector>
#include <cstdint>
#include "inttypes.h"
#include "string.h"

#include "zs_std_func_pkg.cpp"
#include "zs_top_level_pkg.cpp"

zs_axi_formatter::zs_axi_formatter() {
    initialize();
}

void zs_axi_formatter::initialize() {
    word_idx = 0;
    active_word = 0;
}

std::vector<uint64_t> zs_axi_formatter::get_array() {
    //We have to write the half word to the array before sending it out
    if (word_idx == 1) {
        array.push_back(active_word);
        active_word = 0;
        word_idx = 0;
    }

    return (array);
}

//This function assumes the array already exist and need to be extended to insert the new data
//Is a low performance function to be used only in one-run cases and not for continuous runs (e.g. to be used for kernels and not for pixels)
void zs_axi_formatter::append(int l_type, int l_address, int l_value) {
    //if (word_idx == 0) // if word idx = 0, reset word
    //  active_word = 0;

    active_word = format_word_at_position(active_word, word_idx, 1, l_type, l_address, l_value);

    if (word_idx == 1) {
        array.push_back(active_word);
        active_word = 0;
        word_idx = 0;
    } else {
        word_idx = 1;
    }

}

void zs_axi_formatter::append_empty() {

    if (word_idx == 0) {
        array.push_back(0);
    } else {
        array.push_back(active_word);
        array.push_back(0);
        active_word = 0;
        word_idx = 0;
    }

}

void zs_axi_formatter::flush_word() {

    if (word_idx == 1) {
        array.push_back(active_word);
        active_word = 0;
        word_idx = 0;
    }

}

void zs_axi_formatter::append_new_word(int l_type, int l_address, int l_value) {

    uint64_t new_word = format_word_at_position(0, 0, 1, l_type, l_address, l_value);

    if (word_idx == 0) {
        array.push_back(new_word);
    } else {
        array.push_back(active_word);
        array.push_back(new_word);
        active_word = 0;
        word_idx = 0;
    }
}



#endif
