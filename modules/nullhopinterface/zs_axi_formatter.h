#ifndef __ZS_AXI_FORMATTER_H__
#define __ZS_AXI_FORMATTER_H__

#include "stdio.h"
#include <iostream>
#include <vector>
#include <cstdint>
#include "inttypes.h"
#include "string.h"
#include "zs_top_level_pkg.cpp"
#include "npp_std_func_pkg.cpp"
class zs_axi_formatter {
    private:
        uint64_t active_word;
        uint8_t word_idx;
        void initialize();

    public:
        std::vector<uint64_t> array;

        zs_axi_formatter();
        void append(int l_type, int l_address, int l_value);
        void append_new_word(int l_type, int l_address, int l_value);
        void flush_word();
        void append_empty();
        std::vector<uint64_t> get_array();


        //Static methods
        static uint64_t invalidate_word_at_position(const uint64_t l_input_word, const uint8_t position) {
            if (position == 0) {
                return(l_input_word & !zs_axi_bits::FIRST_VALID_MASK);
            } else {
                return(l_input_word & !zs_axi_bits::SECOND_VALID_MASK);
            }
        }

        //Inline methods
        inline static uint64_t format_word0(const uint16_t l_short_value, const uint16_t l_utype, const uint16_t l_uvalid,
                const uint16_t l_uaddress) {
            return ((uint64_t) l_short_value | (uint64_t) l_utype << zs_axi_bits::TYPE_VALUE_SHIFT
                    | (uint64_t) l_uvalid << zs_axi_bits::FIRST_VALID_SHIFT
                    | (uint64_t) l_uaddress << zs_axi_bits::FIRST_ADDR_SHIFT);
        }



        inline uint64_t fast_2pixels_word_format(const uint16_t l_first_value, const uint16_t l_second_value) {
            return ((uint64_t) npp_std::int_to_short(l_first_value)
                    | (uint64_t) npp_std::int_to_short(l_second_value) << zs_axi_bits::SECOND_VALUE_SHIFT
                    | (uint64_t) zs_parameters::IMG_TYPE << zs_axi_bits::TYPE_VALUE_SHIFT
                    | (uint64_t) 1 << zs_axi_bits::FIRST_VALID_SHIFT | (uint64_t) 1 << zs_axi_bits::SECOND_VALID_SHIFT);
        }

        inline uint64_t fast_1pixel_word_format(const uint16_t l_first_value) {
            return ((uint64_t) npp_std::int_to_short(l_first_value)
                    | (uint64_t) zs_parameters::IMG_TYPE << zs_axi_bits::TYPE_VALUE_SHIFT
                    | (uint64_t) 1 << zs_axi_bits::FIRST_VALID_SHIFT);
        }




        inline uint64_t set_new_row_flag(const uint64_t l_old_word, const uint8_t slot) {
            if (slot == 0) {
                return (l_old_word | (uint64_t) zs_address_space::config_image_start_new_row_instr << zs_axi_bits::FIRST_ADDR_SHIFT);
            } else {
                return (l_old_word | (uint64_t) zs_address_space::config_image_start_new_row_instr << zs_axi_bits::SECOND_ADDR_SHIFT);
            }
        }


        //If position == 0 word is created from 0
        inline uint64_t format_word_at_position(const uint64_t l_old_word, const uint8_t l_word_idx, const uint8_t l_valid,
                const uint8_t l_type, const uint16_t l_address, const uint16_t l_value) {

            uint64_t l_formatted_word;

            const uint16_t l_short_value = npp_std::int_to_short(l_value);

            if (l_word_idx == 0) {
                l_formatted_word = format_word0(l_short_value, l_type, l_valid, l_address);
            } else {
                l_formatted_word = (uint64_t) l_old_word | (uint64_t) l_short_value << zs_axi_bits::SECOND_VALUE_SHIFT
                        | (uint64_t) l_valid << zs_axi_bits::SECOND_VALID_SHIFT
                        | (uint64_t) l_address << zs_axi_bits::SECOND_ADDR_SHIFT;
            };
            return (l_formatted_word);
        }
};

#endif
