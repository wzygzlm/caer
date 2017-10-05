/*
 * Npp_performance_profiler.hpp
 *
 *  Created on: Sep 12, 2017
 *      Author: arios
 */

#ifndef AXI_DMA_NPP_PERFORMANCE_PROFILER_HPP_
#define AXI_DMA_NPP_PERFORMANCE_PROFILER_HPP_

#include <chrono>
#include <string>
#include <vector>

class Npp_performance_profiler
{
    private:
        /* Here will be the instance stored. */
        static Npp_performance_profiler* instance;

        /* Private constructor to prevent instancing. */
        Npp_performance_profiler();

        std::vector<std::chrono::time_point<std::chrono::high_resolution_clock>> time_start, time_end;
        std::vector<std::string> labels;
        uint16_t num_checkpoint = 0;
        double avg_axidma_write_transfer_time_per_byte;
        double avg_axidma_read_transfer_time_per_byte;

    public:
        /* Static access method. */
        static Npp_performance_profiler* getInstance();

        uint16_t add_label(const std::string label);
        void start_checkpoint(const uint16_t checkpoint_idx);
        void stop_checkpoint(const uint16_t checkpoint_idx);
        void report_checkpoint(const uint16_t checkpoint_idx);
        double get_report_checkpoint(const uint16_t checkpoint_idx);
        void report();

        void set_avg_axidma_write_transfer_time(double time_per_byte);
        void set_avg_axidma_read_transfer_time(double time_per_byte);
};


#endif /* AXI_DMA_NPP_PERFORMANCE_PROFILER_HPP_ */
