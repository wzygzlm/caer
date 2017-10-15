/*
 * Npp_performance_profiler.cpp
 *
 *  Created on: Sep 12, 2017
 *      Author: arios
 */

#include "npp_performance_profiler.hpp"
#include "npp_log_utilities.cpp"

Npp_performance_profiler* Npp_performance_profiler::instance = 0;

Npp_performance_profiler::Npp_performance_profiler()
{
	//time_start = new std::vector<std::chrono::time_point<std::chrono::high_resolution_clock>>();
	//time_end = new std::vector<std::chrono::time_point<std::chrono::high_resolution_clock>>();
	//labels = new std::vector<std::string>();
	avg_axidma_write_transfer_time_per_byte = 0;
	avg_axidma_read_transfer_time_per_byte = 0;
}


Npp_performance_profiler* Npp_performance_profiler::getInstance()
{
    if (instance == 0)
    {
        instance = new Npp_performance_profiler();
    }

    return instance;
}

uint16_t Npp_performance_profiler::add_label(const std::string label)
{
	std::chrono::high_resolution_clock::time_point empty_time_point = std::chrono::high_resolution_clock::now();
	labels.push_back(label);
	time_start.push_back(empty_time_point);
	time_end.push_back(empty_time_point);
	num_checkpoint++;
	log_utilities::performance("Assigning checkpoint %d to label '%s'",(labels.size() - 1), label.c_str());
	log_utilities::debug("Checkpoint_idx: %d. Time_start vector size: %d. Time_end vector size: %d. Labels vector size: %d",
			num_checkpoint, time_start.size(), time_end.size(), labels.size());
	return (labels.size() - 1);
}

void Npp_performance_profiler::start_checkpoint(const uint16_t checkpoint_idx)
{
	log_utilities::debug("Checkpoint_idx: %d. Time_start vector size: %d. Labels vector size: %d", checkpoint_idx, time_start.size(), labels.size());
	time_start[checkpoint_idx] = std::chrono::high_resolution_clock::now();
}

void Npp_performance_profiler::stop_checkpoint(const uint16_t checkpoint_idx)
{
	log_utilities::debug("Checkpoint_idx: %d. Time_end vector size: %d. Labels vector size: %d", checkpoint_idx, time_end.size(), labels.size());
	time_end[checkpoint_idx] = std::chrono::high_resolution_clock::now();
}

void Npp_performance_profiler::report_checkpoint(const uint16_t checkpoint_idx)
{
	const double time_interval = std::chrono::duration_cast<std::chrono::microseconds>(time_end[checkpoint_idx] - time_start[checkpoint_idx]).count();
	log_utilities::performance(labels[checkpoint_idx] +": %f ms", time_interval/1000);
}

double Npp_performance_profiler::get_report_checkpoint(const uint16_t checkpoint_idx)
{
	double time_interval = std::chrono::duration_cast<std::chrono::microseconds>(time_end[checkpoint_idx] - time_start[checkpoint_idx]).count();
	return (time_interval); //us
}

void Npp_performance_profiler::report()
{
	log_utilities::performance("Performance Report:");
	if (num_checkpoint != 0) {
		for (uint16_t checkpoint_idx = 0; checkpoint_idx < num_checkpoint; checkpoint_idx++) {
			report_checkpoint(checkpoint_idx);
		}
	log_utilities::performance("Avg AXIDMA write transfer per byte: %f us/byte", avg_axidma_write_transfer_time_per_byte);
	log_utilities::performance("Avg AXIDMA read transfer per byte: %f us/byte", avg_axidma_read_transfer_time_per_byte);
	} else {
		log_utilities::warning("No performance checkpoint to report");
	}
}

void Npp_performance_profiler::set_avg_axidma_write_transfer_time(double time_per_byte)
{
	avg_axidma_write_transfer_time_per_byte = (avg_axidma_write_transfer_time_per_byte == 0) ? time_per_byte : (this->avg_axidma_write_transfer_time_per_byte + time_per_byte) / 2;
}

void Npp_performance_profiler::set_avg_axidma_read_transfer_time(double time_per_byte)
{
	avg_axidma_read_transfer_time_per_byte = (avg_axidma_read_transfer_time_per_byte == 0) ? time_per_byte : (this->avg_axidma_read_transfer_time_per_byte + time_per_byte) / 2;
}
