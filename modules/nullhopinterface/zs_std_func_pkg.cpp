#ifndef __ZS_STD_FUNC_PKG__
#define __ZS_STD_FUNC_PKG__
#include  "npp_log_utilities.cpp"
#include "zs_axi_formatter.h"
#include "zs_top_level_pkg.cpp"
#include "npp_std_func_pkg.cpp"
#include <exception>
#include <stdexcept>
#include "zs_top_level_pkg.cpp"
#include "npp_std_func_pkg.cpp"
#include <bitset>         // std::bitset
#include <algorithm>    // std::copy
#include <vector>
#include <chrono>
#include <ctime>

namespace zs_std {

    inline int16_t get_first_value(const uint64_t word) {
        return ((int16_t) (word & zs_axi_bits::FIRST_VALUE_MASK));
    }

    inline std::tuple<int16_t, int, uint8_t> get_next_valid_value(const std::vector<uint64_t> &activations,
            const unsigned int activ_idx, const uint8_t word_idx) {

        if (activ_idx < activations.size()) {
            if (word_idx == 0) {

                const uint64_t first_valid = (uint64_t) activations[activ_idx] & zs_axi_bits::FIRST_VALID_MASK;
                if (first_valid != 0) { //different from 0 rather than equal to 1 to save a shift

                    int16_t first_value = (int16_t) (activations[activ_idx] & zs_axi_bits::FIRST_VALUE_MASK);
                    return (std::make_tuple((int16_t) first_value, (int) activ_idx, (uint8_t) 1)); //word idx returned is 1 so we know we are going to read that word next time
                } else {

                    return (get_next_valid_value(activations, activ_idx + 1, 0)); //word invalid, move to next one
                }
            } else {

                const uint64_t second_valid = (uint64_t) activations[activ_idx] & zs_axi_bits::SECOND_VALID_MASK;

                if (second_valid != 0) { //different from 0 rather than equal to 1 to save a shift
                    const int16_t second_value = (int16_t) ( (activations[activ_idx] & zs_axi_bits::SECOND_VALUE_MASK)
                            >> zs_axi_bits::SECOND_VALUE_SHIFT);

                    return (std::make_tuple((int16_t) second_value, (int) (activ_idx + 1), (uint8_t) 0));
                } else {

                    return (get_next_valid_value(activations, activ_idx + 1, 0));
                }
            }
        } else {

            return (std::make_tuple((int16_t) 0, (int) activations.size(), (uint8_t) 0));
        }
    }

    //Get as input vector of activations and position in which it is found
    //It assumes that if value 0 is invalid, also value 1 is invalid
    //Returns first valid word, the first value, word index
    inline std::tuple<uint64_t, int16_t, unsigned int> get_next_valid_word_first_value(const std::vector<uint64_t> &activations,
            const unsigned int activ_idx) {

        if (activ_idx < activations.size()) {

            bool first_valid = (bool) ( (activations[activ_idx] & zs_axi_bits::FIRST_VALID_MASK) != 0);

            if (first_valid == true) {

                int16_t first_value = (int16_t) (activations[activ_idx] & zs_axi_bits::FIRST_VALUE_MASK);

                return (std::make_tuple(activations[activ_idx], first_value, activ_idx));
            } else {
                return (get_next_valid_word_first_value(activations, activ_idx + 1)); //word invalid, move to next one
            }

        } else {
            log_utilities::debug("get_next_valid_word_values got to vector's end");
            return (std::make_tuple((uint64_t) 0, (int16_t) 0, (unsigned int) activations.size() + 1));
        }
    }

    //Get as input vector of activations and position in which it is found
    //It assumes that if value 0 is invalid, also value 1 is invalid
    //Returns first valid word, the first value, word index
    inline std::tuple<uint64_t, unsigned int> get_next_valid_word(const std::vector<uint64_t> &activations,
            const unsigned int activ_idx) {

        if (activ_idx < activations.size()) {

            const bool first_valid = (bool) ( (activations[activ_idx] & zs_axi_bits::FIRST_VALID_MASK) != 0);

            if (first_valid == true) {

                return (std::make_tuple(activations[activ_idx], activ_idx));
            } else {
                return (get_next_valid_word(activations, activ_idx + 1)); //word invalid, move to next one
            }

        } else {
            log_utilities::debug("get_next_valid_word_values got to vector's end");
            return (std::make_tuple((uint64_t) 0, (unsigned int) activations.size() + 1));
        }
    }

    //Get as input vector of activations and position in which it is found
    //It assumes that if value 0 is invalid, also value 1 is invalid
    //Returns first valid word, the first value, word index
    inline unsigned int get_next_valid_word_idx(std::vector<uint64_t> &activations, const unsigned int activ_idx) {

        if (activ_idx < activations.size()) {

            const bool first_valid = (bool) ( (activations[activ_idx] & zs_axi_bits::FIRST_VALID_MASK) != 0);

            if (first_valid == true) {
                return (activ_idx);
            } else {
                return (get_next_valid_word_idx(activations, activ_idx + 1)); //word invalid, move to next one
            }

        } else {
            log_utilities::debug("get_next_valid_word_values got to vector's end");
            return (activations.size() + 1);
        }
    }

    //Get as input vector of activations and position in which it is found
    //It assumes that if value 0 is invalid, also value 1 is invalid
    //Returns first valid word, the two valid, and its index
    inline std::tuple<uint64_t, int16_t, int16_t, bool, bool, unsigned int> get_next_valid_word_values(
            std::vector<uint64_t> &activations, const unsigned int activ_idx) {

        if (activ_idx < activations.size()) {

            const bool first_valid = (bool) ( (activations[activ_idx] & zs_axi_bits::FIRST_VALID_MASK) != 0);

            if (first_valid == true) { //}) || second_valid == true) {
                const bool second_valid = (bool) ( (activations[activ_idx] & zs_axi_bits::SECOND_VALID_MASK) != 0);
                const int16_t first_value = (int16_t) (activations[activ_idx] & zs_axi_bits::FIRST_VALUE_MASK);
                const int16_t second_value = (int16_t) ( (activations[activ_idx] & zs_axi_bits::SECOND_VALUE_MASK)
                        >> zs_axi_bits::SECOND_VALUE_SHIFT);

                return (std::make_tuple(activations[activ_idx], first_value, second_value, first_valid, second_valid, activ_idx));
            } else {
                return (get_next_valid_word_values(activations, activ_idx + 1)); //word invalid, move to next one
            }

        } else {
            log_utilities::debug("get_next_valid_word_values got to vector's end");
            return (std::make_tuple((uint64_t) 0, (int16_t) 0, (int16_t) 0, false, false, (unsigned int) activations.size() + 1));
        }
    }

    //This function return 64 bit data since we need to shift the value left by MANTISSA_NUM_BITS anyway.
    //This is a low performance function designed for TB purposes.
    inline std::vector<std::vector<std::vector<int64_t>>>decompress_sm_image(std::vector<uint64_t> &input, const unsigned int num_rows, const unsigned int num_columns,
            const unsigned int num_channels, const unsigned int sm_length) {

        log_utilities::debug("Starting image decompression as 3D image...");

        uint16_t current_sm = 0;
        uint8_t in_word_idx = 0;
        uint16_t row = 0;
        uint16_t row_flag_counter = 0;
        uint16_t column = 0;
        uint16_t channel = 0;
        uint8_t sm_position = sm_length;
        unsigned int input_word_idx = 0; //the full get_next_word function updates also input_idx, but in this case we dont need it so we save it into an unused variable
        std::vector<std::vector<std::vector<int64_t>>> output_image(num_rows);

        for (unsigned int row_idx = 0; row_idx < num_rows; row_idx++) {
            output_image[row_idx].resize(num_columns);
            for(unsigned int column_idx = 0; column_idx < num_columns; column_idx++) {
                output_image[row_idx][column_idx].resize(num_channels);
            }
        }

        log_utilities::debug("Image placeholder generated");
        log_utilities::debug("Total number of words: %d, num_rows: %d, num_columns: %d, num_channels: %d",input.size(),num_rows, num_columns,
                num_channels);

        do {
            int16_t next_word;
            uint64_t address;
            std::tie(next_word, input_word_idx, in_word_idx) = get_next_valid_value(input, input_word_idx, in_word_idx);

            if (input_word_idx < input.size()) {

                if (in_word_idx == 1) { //Index is inverted since in_word_idx is pointing at NEXT position to be read, not current one
                    address = (input[input_word_idx] & (zs_axi_bits::FIRST_ADDRESS_MASK)) >> zs_axi_bits::FIRST_ADDR_SHIFT;
                } else {
                    address = (input[input_word_idx] & (zs_axi_bits::SECOND_ADDRESS_MASK)) >> zs_axi_bits::SECOND_ADDR_SHIFT;
                }
            }

            if (sm_position == sm_length) { //the word is a sm
                current_sm = next_word;
                sm_position = 0;

                //check if new row set properly
                if (column == 0 && channel == 0) {

                    if (address == 1) {
                        log_utilities::debug("Row %d start flag matched (A) - input_word_idx: %d",row,input_word_idx );
                        row_flag_counter++;
                    } else {
                        if (input_word_idx == input.size() -1 ) { //To avoid false error at last word due to zero fillers
                            log_utilities::error("ERROR: Missing new row flag at row %d (A) - word: %llu", row, input[input_word_idx]);
                            //throw std::invalid_argument("Invalid input to decompression algorithm");
                        }
                    }

                } else {
                    if (address == 1) {
                        log_utilities::error("ERROR: Incorrect new row flag on SM at input_word_idx: %d row: %d column: %d channel: %d ", input_word_idx,row, column, channel );
                        //throw std::invalid_argument("Invalid input to decompression algorithm");
                    }
                }

                if (current_sm == 0) { //new sm is 0

                    //It means there are zeros at the end of the input array so we dont need to update the SM and we have done
                    if (input_word_idx == input.size()) {
                        break;
                    }

                    for (uint8_t sm_idx = 0; sm_idx < sm_length; sm_idx++) {
                        std::tie(channel, column, row) = npp_std::update_3d_indices(channel, column, row, num_channels,num_columns, num_rows);

                    }
                    sm_position = sm_length;
                }

            } else {

                //get coordinates
                for (uint8_t sm_idx = sm_position; sm_idx < sm_length; sm_idx++) {
                    if ((current_sm & (1 << sm_idx)) != 0) {
                        current_sm = current_sm & (~(1 << sm_idx));
                        sm_position = sm_idx+1;
                        int64_t new_value = next_word;         //cast to int64_t
                        output_image[row][column][channel] = new_value;

                        if (address == 1) {
                            log_utilities::error("Incorrect new row flag on PIXEL at input_word_idx: %d row: %d column: %d channel: %d ", input_word_idx,row, column, channel );
                            //  throw std::invalid_argument("Invalid input to decompression algorithm");
                        }

                        if (new_value == 0) {
                            if ( (next_word & (zs_axi_bits::FIRST_VALID_MASK | zs_axi_bits::SECOND_VALID_MASK )) != 0)
                            log_utilities::error("Zero pixel found in compressed image at position: %d %d %d ",row, column, channel);

                        }

                        if (address == 1) {
                            log_utilities::error("New row flag asserted on pixel instead of sm - coord: row: %d column: %d channel: %d", row, column, channel);

                        }

                        std::tie(channel, column, row) = npp_std::update_3d_indices(channel, column, row, num_channels,num_columns, num_rows);

                        break;
                    } else {
                        sm_position = sm_idx+1;
                        std::tie(channel, column, row) = npp_std::update_3d_indices(channel, column, row, num_channels,num_columns, num_rows);

                        if (sm_position == sm_length) {

                            current_sm = next_word;
                            sm_position = 0;

                            //check if new row set properly
                            if (column == 0 && channel == 0) {

                                if (address == 1) {
                                    log_utilities::debug("Row %d start flag matched (B) - input_word_idx: %d",row,input_word_idx );
                                    row_flag_counter++;
                                } else {
                                    if (input_word_idx == input.size() -1 ) { //To avoid false error at last word due to zero fillers
                                        log_utilities::error("Missing new row flag at row %d (B) - word: %llu", row, input[input_word_idx]);

                                    }
                                }

                            } else {
                                if (address == 1) {
                                    log_utilities::error("Incorrect new row flag on SM at input_word_idx: %d row: %d column: %d channel: %d ", input_word_idx,row, column, channel );
                                    //throw std::invalid_argument("Invalid input to decompression algorithm");
                                }
                            }

                            if (current_sm == 0) { //new sm is 0

                                for (uint8_t sm_idx = 0; sm_idx < sm_length; sm_idx++) {
                                    std::tie(channel, column, row) = npp_std::update_3d_indices(channel, column, row, num_channels,num_columns, num_rows);

                                }
                                sm_position = sm_length;
                            }

                        }
                    }

                }

            }
        }while (input_word_idx < input.size());

        if (num_rows-1 != row && row != 0) {
            log_utilities::error("Mismatch in number of rows - num_rows: %d, row %d",num_rows, row);
        }

        log_utilities::debug("Matched %d new row flags",row_flag_counter);
        if (row_flag_counter != num_rows) {
            log_utilities::error("Mismatch in row flag numbers -  row_flag_counter: %d,  num_rows %d", row_flag_counter,num_rows);
        }

        log_utilities::debug("Decompression done");
        return(output_image);
    }

    //Values are reshifted into real value
    //Output image must be of the exact size to hold the decompressed image, if not result is undefined
    inline void decompress_sm_image_as_linear_vector(const std::vector<uint64_t> &input, const uint8_t sm_length,
            std::vector<int16_t> &output_image) {

        log_utilities::debug("Starting image decompression as linear vector...");

        uint16_t current_sm = 0;
        uint8_t in_word_idx = 0;
        uint8_t sm_position = sm_length;
        unsigned int input_word_idx = 0; //the full get_next_word function updates also input_idx, but in this case we dont need it so we save it into an unused variable
        unsigned int input_size = input.size();
        unsigned int output_idx = 0;

        do {
            int16_t next_word;
            std::tie(next_word, input_word_idx, in_word_idx) = get_next_valid_value(input, input_word_idx, in_word_idx);

            if (sm_position == sm_length) { //the word is a sm

                current_sm = next_word;
                sm_position = 0;

                if (current_sm == 0) { //new sm is 0

                    //It means there are zeros at the end of the input array so we dont need to update the SM and we have done
                    if (input_word_idx == input_size) {
                        break;
                    }

                    for (size_t sm_idx = sm_position; sm_idx < sm_length; sm_idx++) {
                        output_image[output_idx] = 0;
                        output_idx++;
                    }
                    sm_position = sm_length;
                }

            } else {

                //get coordinates
                for (unsigned int sm_idx = sm_position; sm_idx < sm_length; sm_idx++) {
                    if ( (current_sm & (1 << sm_idx)) != 0) {
                        current_sm = current_sm & ( ~ (1 << sm_idx));
                        sm_position = sm_idx + 1;
                        int64_t new_value = next_word;         //cast to int64_t
                        output_image[output_idx] = new_value;
                        output_idx++;

                        break;         // TODO performance can be improved removing this break
                    } else {
                        sm_position = sm_idx + 1;
                        output_image[output_idx] = 0;
                        output_idx++;

                        if (sm_position == sm_length) {

                            //log_utilities::debug("New sm detected (mode2) %d", next_word);
                            current_sm = next_word;
                            sm_position = 0;

                            if (current_sm == 0) { //new sm is 0
                                //It means there are zeros at the end of the input array so we dont need to update the SM and we have done
                                if (input_word_idx == input_size) {
                                    break;
                                }
                                for (unsigned int sm_idx = sm_position; sm_idx < sm_length; sm_idx++) {
                                    output_image[output_idx] = 0;
                                    output_idx++;
                                }
                                sm_position = sm_length;
                            }
                        }
                    }
                }
            }
        } while (output_idx < output_image.size());

        log_utilities::debug("Decompression done");

    }

    //This function takes a linear,uncompressed input image obtained with stride 1 and translate it to new selected stride
    //Notice stride MUST be > 1 or the algorithm will not work
    //TODO need optimization for run on FPGA
    //TODO Never used, never debugged
    inline void activations_stride_shrink(std::vector<uint64_t> &activations, const uint8_t stride) {

        const unsigned int activations_size = activations.size();

        std::vector<bool> item_to_keep(activations_size * 2, false); //TImes two because we need a true/false for each word

#pragma omp parallel for num_threads(4) schedule(dynamic)
        for (unsigned int activ_idx = 0; activ_idx < activations_size * 2; activ_idx++) {

            if (activ_idx % stride == 0) {
                item_to_keep[activ_idx] = true;
            }

        }

        unsigned int first_check_idx = 0;
        unsigned int second_check_idx = 1;
        unsigned int shrink_idx = 0;
#pragma omp parallel for num_threads(4) schedule(dynamic)
        for (unsigned int activ_idx = 0; activ_idx < activations_size; activ_idx++) {

            if (item_to_keep[first_check_idx] == false) {
                activations[activ_idx] = zs_axi_formatter::invalidate_word_at_position(activations[activ_idx], 0);

                if (item_to_keep[second_check_idx] == false) {
                    activations[activ_idx] = zs_axi_formatter::invalidate_word_at_position(activations[activ_idx], 1);
                }
            } else { //If the first is true we dont need to check, we already know that the next is false (stride > 1 in this function)
                activations[activ_idx] = zs_axi_formatter::invalidate_word_at_position(activations[activ_idx], 1);
            }

            //  shrinked_activations[shrink_idx]

            first_check_idx = first_check_idx + 2;
            second_check_idx = second_check_idx + 2;

        }

    }

    inline void activations_reorder_no_pooling_relu_on(const std::vector<uint64_t> &activations,
            std::vector<uint64_t> &reordered_activations, const uint16_t num_row, const uint32_t sm_per_row,
            const uint16_t consecutive_sm) {

        const uint32_t num_of_parts = sm_per_row / consecutive_sm;
        unsigned int word_idx = 0;
        unsigned int reorder_activations_idx = 0;
        const uint16_t half_num_row = num_row / 2;

        std::vector<unsigned int> row_b_starts(num_of_parts);
        std::vector<unsigned int> row_b_ends(num_of_parts);

        log_utilities::full("Reordering activations for no pooling mode...");
        log_utilities::full("num_row: %d", num_row);
        log_utilities::full("half_num_row: %d", half_num_row);
        log_utilities::full("sm_per_row: %d", sm_per_row);
        log_utilities::full("consecutive_sm: %d", consecutive_sm);
        log_utilities::full("num_of_parts: %d", num_of_parts);
        log_utilities::full("activations.size(): %d", activations.size());

        reordered_activations.resize(activations.size() + 1);

        uint32_t num_pixels_tot = 0;
        for (uint16_t row_idx = 0; row_idx < half_num_row; row_idx++) {
            log_utilities::debug("Row couple idx: %d", row_idx);
            for (uint32_t part_idx = 0; part_idx < num_of_parts; part_idx++) {
                //log_utilities::debug("Part_idx %d over %d", part_idx,num_of_parts);
                //ROW A
                for (uint16_t sm_idx = 0; sm_idx < consecutive_sm; sm_idx++) {
                    const uint64_t next_word = activations[word_idx];
                    const uint16_t first_value = get_first_value(next_word);
                    reordered_activations[reorder_activations_idx] = next_word;
                    reorder_activations_idx++;
                    word_idx++;
                    //  log_utilities::debug("LOOP A: word_idx %d - SM %d", word_idx,first_value);
                    //we already written the first pixel writing the previous word, thus the -1 (notice datatype must be signed)
                    const int8_t num_pixels = npp_std::count_ones(first_value) - 1;
                    num_pixels_tot = num_pixels_tot + 16;
                    //We are assuming no empty words in between sm and their pixels
                    for (int8_t pixel_idx = 0; pixel_idx < num_pixels; pixel_idx = pixel_idx + 2) {
                        reordered_activations[reorder_activations_idx] = activations[word_idx];
                        reorder_activations_idx++;
                        word_idx++;
                        //      log_utilities::debug("LOOP A: word_idx %d - Pixel", word_idx);
                    }
                }

                row_b_starts[part_idx] = word_idx;

                if (word_idx > activations.size()) {
                    log_utilities::debug("SAVING START: word_idx %d larger that size %d", word_idx, activations.size());

                }

                //ROW B
                for (uint16_t sm_idx = 0; sm_idx < consecutive_sm; sm_idx++) {
                    const uint64_t next_word = activations[word_idx];
                    const uint16_t first_value = get_first_value(next_word);

                    const int8_t num_pixels = npp_std::count_ones(first_value);
                    num_pixels_tot = num_pixels_tot + 16;
                    word_idx = word_idx + (num_pixels / 2) + 1;
                    // log_utilities::debug("LOOP B: word_idx %d - SM %d", word_idx, first_value);
                }
                row_b_ends[part_idx] = word_idx;

                if (word_idx > activations.size()) {
                    log_utilities::debug("SAVING END: word_idx %d larger that size %d", word_idx, activations.size());

                }

            }
            log_utilities::debug("Total pixel double row %d", num_pixels_tot);

            for (uint16_t row_b_part_idx = 0; row_b_part_idx < num_of_parts; row_b_part_idx++) {
                const unsigned int part_start = row_b_starts[row_b_part_idx];
                const unsigned int part_end = row_b_ends[row_b_part_idx];

                if (part_start > activations.size()) {
                    log_utilities::debug("COPY: part_start %d larger that size %d", part_start, activations.size());
                }

                if (part_end > activations.size()) {
                    log_utilities::debug("COPY: part_end %d larger that size %d", part_end, activations.size());
                }

                if (reorder_activations_idx > reordered_activations.size()) {
                    log_utilities::debug("COPY: reorder_activations_idx %d larger that size %d", part_end,
                            reordered_activations.size());
                }

                std::copy(activations.begin() + part_start, activations.begin() + part_end,
                        reordered_activations.begin() + reorder_activations_idx);

                reorder_activations_idx = reorder_activations_idx + part_end - part_start;
            }
        }

        if (num_row % 2 == 1) { // CURRENTLY NOT SUPPORTED BY HW (why? was supposed to be)
            log_utilities::full("Odd number row for no pooling reordering");
            log_utilities::error("Operation currently not supported by HW");
            //XXX Keep following code, is it working - just hw doesnt support it
            /*for (unsigned int part_idx = 0; part_idx < num_of_parts; part_idx++) {
             //ROW A
             for (unsigned int sm_idx = 0; sm_idx < consecutive_sm; sm_idx++) {
             const uint64_t next_word = activations[word_idx];
             const uint16_t first_value = get_first_value(next_word);
             reordered_activations[reorder_activations_idx] = next_word;
             reorder_activations_idx++;
             word_idx++;

             //we already written the first pixel writing the previous word, thus the -1 (notice datatype must be signed)
             const int8_t num_pixels = npp_std::count_ones(first_value) - 1;
             //We are assuming no empty words in between sm and their pixels
             for (int pixel_idx = 0; pixel_idx < num_pixels; pixel_idx = pixel_idx + 2) {
             reordered_activations[reorder_activations_idx] = activations[word_idx];
             reorder_activations_idx++;
             word_idx++;
             if (word_idx >= activations.size()) {
             log_utilities::error("A Indexing error!");
             }
             if (reorder_activations_idx >= reordered_activations.size()) {
             log_utilities::error("A reo Indexing error!");
             }

             }

             }

             //ROW B
             for (unsigned int sm_idx = 0; sm_idx < consecutive_sm; sm_idx++) {
             const uint64_t next_word = activations[word_idx];
             const uint16_t first_value = get_first_value(next_word);
             const uint8_t num_pixels = npp_std::count_ones(first_value);

             word_idx = word_idx + (num_pixels / 2) + 1;
             if (word_idx >= activations.size()) {
             log_utilities::error("B Indexing error!");
             }
             }

             }*/
        }
        log_utilities::debug("Setting last word for image load done pulse...");

        reordered_activations[reorder_activations_idx] = zs_axi_formatter::format_word0((uint16_t) 1,
                (uint16_t) zs_parameters::REG_TYPE, (uint16_t) 1, (uint16_t) zs_address_space::config_image_load_done_pulse);

        log_utilities::debug("Resizing...");
        reordered_activations.resize(reorder_activations_idx + 1); //resize so we remove the zeros

        log_utilities::debug("Reordering completed");

    }

    inline void activations_multipass_merge(const std::vector<std::vector<uint64_t>> &layer_results,
            std::vector<uint64_t> &merged_result, const uint32_t total_num_sm, const uint16_t num_sm_per_channel_per_pass) {

        log_utilities::full("Starting multipass merge...");

        uint32_t merged_size = layer_results[0].size();

        for (uint16_t layer_idx = 1; layer_idx < layer_results.size(); layer_idx++) {
            merged_size = merged_size + layer_results[layer_idx].size();
        }

        uint32_t merged_idx = 0;
        const uint16_t num_pass = layer_results.size();
        const uint16_t num_sm_per_channel = num_sm_per_channel_per_pass * num_pass;

        std::vector<uint32_t> pass_word_idx(num_pass);

        log_utilities::debug("total_num_sm:%d,num_sm_per_channel_per_pass: %d, num_pass: %d, num_sm_per_channel: %d",
                total_num_sm, num_sm_per_channel_per_pass, num_pass, num_sm_per_channel);

        merged_result.resize( (merged_size));
        //We write num_channels_per_pass/SM_SIZE values from each input vector, store them and continue
        for (uint32_t read_sm = 0; read_sm < total_num_sm; read_sm = read_sm + num_sm_per_channel) {
            for (uint16_t pass_idx = 0; pass_idx < num_pass; pass_idx++) {
                for (uint16_t sm_idx = 0; sm_idx < num_sm_per_channel_per_pass; sm_idx++) {

                    const uint64_t next_word = layer_results[pass_idx][pass_word_idx[pass_idx]];
                    const uint16_t first_value = get_first_value(next_word);
                    merged_result[merged_idx] = next_word;
                    pass_word_idx[pass_idx]++;
                    merged_idx++;

                    //we already written the first pixel writing the previous word, thus the -1
                    const int8_t num_pixels = npp_std::count_ones(first_value) - 1;
                    //We are assuming no empty words in between sm and their pixels
                    for (int8_t pixel_idx = 0; pixel_idx < num_pixels; pixel_idx = pixel_idx + 2) {
                        merged_result[merged_idx] = layer_results[pass_idx][pass_word_idx[pass_idx]];
                        pass_word_idx[pass_idx]++;
                        merged_idx++;

                    }
                }
            }
        }
        //Adding image load done for next pass.
        merged_result[merged_idx] = zs_axi_formatter::format_word0((uint16_t) 1, (uint16_t) zs_parameters::REG_TYPE, (uint16_t) 1,
                (uint16_t) zs_address_space::config_image_load_done_pulse);

        merged_result.resize(merged_idx + 1);
        log_utilities::full("Multipass merge done");

    }

}

#endif

