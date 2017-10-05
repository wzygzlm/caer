#ifndef __ZS_BACKEND_INTERFACE__
#define __ZS_BACKEND_INTERFACE__

#include  "npp_log_utilities.cpp"

#include "zs_backend_interface.h"
#include "stdio.h"
#include "iostream"
#include <string>
#include <vector>
#include <bitset>
#include <exception>
#include <stdexcept>
#include "zs_top_level_pkg.cpp"

#ifdef FPGA_MODE
#include "zs_axi_dma_lib.hpp"
#include "axi_dma_lib.hpp"
#include "axi_dma_pkg.cpp"
#endif

#include "stdio.h"
#include "iostream"
#include "string.h"
#include <vector>

#ifdef FPGA_MODE
#include "zs_axi_dma_lib.hpp"
#endif

#ifdef RTL_MODE
#include "svdpi.h"
//SV functions to be used in RTL mode
//to be placed here in the code to allow the compiler to work properly
extern "C" int simulation_step();
extern "C" void write_word_to_rtl(long long int);
#endif

zs_backend_interface::zs_backend_interface() {
//We delete old log files at the start of the run opening and closing them in w mode
#ifdef SW_TO_ZS_WORDS_LOG
    sw_to_zs_words_file = fopen("sw_to_zs_words.log", "w");
    fclose(sw_to_zs_words_file);
#endif

#ifdef ZS_TO_SW_WORDS_LOG
    zs_to_sw_words_file = fopen("zs_to_sw_words.log", "w");
    fclose(zs_to_sw_words_file);
#endif

#ifdef FPGA_MODE
    //axi_interface.stop();
    //  axi_interface.reset();
    log_utilities::medium("initializing axi bus");
    axi_interface.init();
    log_utilities::medium("axi bus initialized");
#endif
#ifdef RTL_MODE
    rtl_read_word = 0;
    new_rtl_read_word_available = false;
#endif
}

void zs_backend_interface::print_sw_to_zs_words(std::vector<uint64_t> array) {
#ifdef SW_TO_ZS_WORDS_LOG
    sw_to_zs_words_file = fopen("sw_to_zs_words.log", "a");
    print_axi_words(array, sw_to_zs_words_file);
    fclose(sw_to_zs_words_file);
#endif
}

void zs_backend_interface::print_zs_to_sw_words(std::vector<uint64_t> array) {
#ifdef ZS_TO_SW_WORDS_LOG
    zs_to_sw_words_file = fopen("zs_to_sw_words.log", "a");
    print_axi_words(array, zs_to_sw_words_file);
    fclose(zs_to_sw_words_file);
#endif
}

void zs_backend_interface::print_axi_words(std::vector<uint64_t> array, FILE* file) {
#if defined(ZS_TO_SW_WORDS_LOG) || defined(SW_TO_ZS_WORDS_LOG)
    static const uint64_t FIRST_VALUE_MASK = zs_axi_bits::FIRST_VALUE_MASK;
    static const uint64_t SECOND_VALUE_MASK = zs_axi_bits::SECOND_VALUE_MASK;
    static const uint64_t TYPE_MASK = zs_axi_bits::TYPE_MASK;
    static const uint64_t VALID_MASK = zs_axi_bits::VALID_MASK;
    static const uint64_t FIRST_ADDRESS_MASK = zs_axi_bits::FIRST_ADDRESS_MASK;
    static const uint64_t SECOND_ADDRESS_MASK = zs_axi_bits::SECOND_ADDRESS_MASK;

    static const int SECOND_VALUE_SHIFT = zs_axi_bits::SECOND_VALUE_SHIFT;
    static const int TYPE_VALUE_SHIFT = zs_axi_bits::TYPE_VALUE_SHIFT;
    static const int FIRST_VALID_SHIFT = zs_axi_bits::FIRST_VALID_SHIFT;
    static const int SECOND_VALID_SHIFT = zs_axi_bits::SECOND_VALID_SHIFT;
    static const int FIRST_ADDR_SHIFT = zs_axi_bits::FIRST_ADDR_SHIFT;
    static const int SECOND_ADDR_SHIFT = zs_axi_bits::SECOND_ADDR_SHIFT;

    for (int entry_idx = 0; entry_idx < array.size(); entry_idx++) {
        uint64_t full_word = array[entry_idx];
        int16_t first_value = (int16_t) full_word & FIRST_VALUE_MASK;
        int16_t second_value = (int16_t) ((full_word & SECOND_VALUE_MASK) >> SECOND_VALUE_SHIFT);
        uint64_t type = (uint64_t) (full_word & TYPE_MASK) >> TYPE_VALUE_SHIFT;
        uint64_t valid = (uint64_t) (full_word & VALID_MASK) >> FIRST_VALID_SHIFT;
        uint64_t first_address = (uint64_t) (full_word & FIRST_ADDRESS_MASK) >> FIRST_ADDR_SHIFT;
        uint64_t second_address = (uint64_t) (full_word & SECOND_ADDRESS_MASK) >> SECOND_ADDR_SHIFT;

        std::bitset<64> bit_repr(array[entry_idx]);
        std::string string_bit_repr = bit_repr.to_string();
        string_bit_repr.insert(zs_parameters::AXI_WIDTH - SECOND_VALUE_SHIFT, " ");
        string_bit_repr.insert(zs_parameters::AXI_WIDTH - TYPE_VALUE_SHIFT, " ");
        string_bit_repr.insert(zs_parameters::AXI_WIDTH - FIRST_VALID_SHIFT, " ");
        string_bit_repr.insert(zs_parameters::AXI_WIDTH - FIRST_ADDR_SHIFT, " ");
        string_bit_repr.insert(zs_parameters::AXI_WIDTH - SECOND_ADDR_SHIFT, " ");
        string_bit_repr.insert(zs_parameters::AXI_WIDTH - SECOND_ADDR_SHIFT - zs_axi_bits::ADDRESS_SIZE, " ");

        double first_word_full_precision = (double) (first_value);
        double second_word_full_precision = (double) (second_value);
        first_word_full_precision = (double) (first_word_full_precision / std::pow(2,zs_parameters::MANTISSA_NUM_BITS));
        second_word_full_precision = (double) (second_word_full_precision / std::pow(2,zs_parameters::MANTISSA_NUM_BITS));

        fprintf(file, "%s - SA:%3d - FA:%3d - VA:%2d - TY:%2d - SV:%6d - FV:%6d - SVS:%4.8f - FVS:%4.6f\n", string_bit_repr.c_str(), second_address, first_address,
                valid, type, second_value, first_value, second_word_full_precision,first_word_full_precision );

        //fprintf(file, "SA:%llu - FA:%llu - VA:%llu - TY:%llu - SV:%llu - FV:%llu\n\n", SECOND_ADDRESS_MASK,FIRST_ADDRESS_MASK,VALID_MASK,TYPE_MASK,SECOND_VALUE_MASK,FIRST_VALUE_MASK);

    }
#endif
}

bool zs_backend_interface::write(const std::vector<uint64_t> *array) {
    log_utilities::high("SW Backend waiting for write to complete...");

    //Print function is empty is compile flag are not set
    print_sw_to_zs_words( *array);

#ifdef FPGA_MODE
    axi_interface.write(array);
#endif

#ifdef RTL_MODE

    log_utilities::debug("Num words received in backend write: %d", array->size());

    long long int word = array->data()[0];
    //In RTL mode right now the burst size is fixed to be always the max one

    if (array->size() > 128000) {
        write_word_to_rtl(word | axi_parameters::OPERATING_MODE_COMPLETE_SHIFTED_VALID);    //setup burst size
    } else {
        write_word_to_rtl(word | axi_parameters::OPERATING_MODE_PARTIAL_SHIFTED_VALID);    //setup burst size
    }
    int split_transfer_counter = 0;
    for (int word_idx = 1; word_idx < array->size(); word_idx++) {
        word = array->data()[word_idx];

        if (split_transfer_counter==128000) {
            split_transfer_counter = 0;
            if (split_transfer_counter + 128000 > array->size()) {
                word = word | axi_parameters::OPERATING_MODE_PARTIAL_SHIFTED_VALID;
            } else {
                word = word | axi_parameters::OPERATING_MODE_COMPLETE_SHIFTED_VALID;
            }
        } else {
            split_transfer_counter++;
        }

        write_word_to_rtl(word);

    }

#endif

    log_utilities::high("Write call completed");
    return (true); //TODO no check for successfull write
}

#ifdef RTL_MODE
void zs_backend_interface::append_new_rtl_word(const uint64_t new_word) {
    rtl_read_word = new_word;
    new_rtl_read_word_available = true;
}
#endif

void zs_backend_interface::read(std::vector<uint64_t> &read_array) {
    log_utilities::high("SW Backend waiting for read to complete...");
    read_array.resize(0);

    log_utilities::debug("read_array.size() pre read: %d", read_array.size());

#ifdef FPGA_MODE
    if (axi_interface.readLayer(&read_array) == -1) {

        throw std::runtime_error("Error in axi read");

    }
#endif

#ifdef RTL_MODE
    bool zs_idle = false;
    unsigned int word_received_counter = 0;
    unsigned int burst_counter = 1;
    //Step with simulation until data arrives
    //Data arrives when the word is different from 0 (without considering IDLE that can be 1)
    rtl_read_word = 0;
    do {
        simulation_step();
    }while ((rtl_read_word & ~zs_axi_bits::IDLE_MASK) == 0);

    log_utilities::high("ZS computation start detected");

    unsigned int burst_word_counter = 0;
    read_array.resize(axi_parameters::AXI_TRANSFER_LENGTH_WORDS);
    do {

        if (new_rtl_read_word_available == true) {
            new_rtl_read_word_available = false;

            read_array[word_received_counter] = rtl_read_word;
            burst_word_counter++;
            word_received_counter++;
            //std::bitset<64> bit_repr(rtl_read_word);
            //std::string string_bit_repr = bit_repr.to_string();

            //if (read_array.size() % axi_parameters::AXI_TRANSFER_LENGTH_WORDS == 0) {
            if (burst_word_counter == axi_parameters::AXI_TRANSFER_LENGTH_WORDS) {
                burst_word_counter = 0;
                burst_counter++;
                std::bitset<64> bit_repr(rtl_read_word);
                std::string string_bit_repr = bit_repr.to_string();
                // log_utilities::debug("Burst end detected: read_array.size: %d - %s",read_array.size(),string_bit_repr.c_str());

                if (bit_repr[zs_axi_bits::IDLE_SHIFT] == 1) {

                    log_utilities::debug("IDLE word received");
                    zs_idle = true;
                } else {
                    zs_idle = false;
                    read_array.resize(axi_parameters::AXI_TRANSFER_LENGTH_WORDS*burst_counter);
                }

            }
            rtl_read_word = 0;
        }

        simulation_step();
    }while (zs_idle == false);

#endif

    print_zs_to_sw_words(read_array);

    log_utilities::debug("read_array.size() post read: %d", read_array.size());
    log_utilities::high("Read call completed");

}

#endif
