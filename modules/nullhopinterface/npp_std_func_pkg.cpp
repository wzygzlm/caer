#ifndef __NPP_STD_FUNC_PKG__
#define __NPP_STD_FUNC_PKG__
/*
 * npp_std_func_pkg.cpp
 *
 *  Created on: Dec 15, 2016
 *      Author: asa
 */

#include "stdio.h"
#include <tuple>
#include <vector>
#include <cstdint>
#include "inttypes.h"
#include <bitset>         // std::bitset
#include  "npp_log_utilities.cpp"

namespace npp_std {
    //Read next integer in file and discard everything until new line
    inline int read_int_from_file(FILE*file) {
        int value;
        fscanf(file, "%d%*[^\n]", &value);
        return (value);

    }

    inline uint16_t int_to_short(const int data) {
        uint16_t newData;

        if (data < 0) {
            newData = (uint16_t) ~ (data - 1);
            newData = ( ~newData) + 1;
        } else {
            newData = (uint16_t) data;
        }
        return (newData);

    }

    //Increment indices in order to loop over 3d images in range 0-(MAX-1)
    //Implemented as ifs since faster than series of divisions
    inline std::tuple<int, int, int> update_3d_indices(int index0, int index1, int index2, int max_index0, int max_index1,
            int max_index2) {

        index0++;

        if (index0 == max_index0) {
            index0 = 0;
            index1++;
        } else {
            return (std::make_tuple(index0, index1, index2));
        }

        if (index1 == max_index1) {
            index1 = 0;
            index2++;
        } else {
            return (std::make_tuple(index0, index1, index2));
        }

        if (index2 == max_index2) {
            index2 = 0;
        }

        return (std::make_tuple(index0, index1, index2));
    }

    inline void remove_words_using_mask(std::vector<uint64_t> &array, const uint64_t mask) {

        for (unsigned int entry_idx = 0; entry_idx < array.size(); entry_idx++) {

            if ( (array[entry_idx] & mask) != 0) {
                array.erase(array.begin() + entry_idx);
                entry_idx--; //we decrease entry idx to avoid skipping one value when erasing
            }
        }
    }

    inline void remove_words_using_mask_and_key(std::vector<uint64_t> &array, const uint64_t mask, const uint64_t key) {
        int found_counter = 0;
        for (unsigned int entry_idx = 0; entry_idx < array.size(); entry_idx++) {

            if ( (array[entry_idx] & mask) == key) {
                array.erase(array.begin() + entry_idx);
                entry_idx--; //we decrease entry idx to avoid skipping one value when erasing
                found_counter++;
            }
        }
        //  log_utilities::error("found_counter: %d over %d - key: %llu mask: %llu",found_counter,array.size(), key,mask);
    }

    //Increment indices in order to loop over 4d images in range 0-(MAX-1)
    //Implemented as ifs since faster than series of divisions in most systems
    inline std::tuple<int, int, int, int> update_4d_indices(int index0, int index1, int index2, int index3, int max_index0,
            int max_index1, int max_index2, int max_index3) {

        index0++;

        if (index0 == max_index0) {
            index0 = 0;
            index1++;
        } else {
            return (std::make_tuple(index0, index1, index2, index3));
        }

        if (index1 == max_index1) {
            index1 = 0;
            index2++;
        } else {
            return (std::make_tuple(index0, index1, index2, index3));
        }

        if (index2 == max_index2) {
            index2 = 0;
            index3++;
        } else {
            return (std::make_tuple(index0, index1, index2, index3));
        }

        if (index3 == max_index3) {
            index3 = 0;
        }

        return (std::make_tuple(index0, index1, index2, index3));
    }

    inline uint8_t count_ones(const uint16_t value) {
        std::bitset<16> bitvalue(value);
        return (bitvalue.count());
    }

    inline uint8_t count_zeros_until_first_one(const uint16_t value) {
        std::bitset<16> bitvalue(value);
        uint8_t bit_idx = 0;
        while (bit_idx < 16 && !bitvalue[bit_idx]) {
            bit_idx++;
        }
        return (bit_idx);

    }

}

#endif
