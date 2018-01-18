/*
 *	Low level functions to control the AXIDMA device
 *	author: Antonio Rios (arios@atc.us.es)
 */

#ifndef __AXIDMA_H__
#define __AXIDMA_H__

#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <math.h>
#include <chrono>
#include <exception>
#include <ctime>
#include <ratio>
#include <stdexcept>
#include "axi_channel_timeout_excep.cpp"
#include "npp_log_utilities.cpp"
#include "npp_performance_profiler.hpp"
#include "axi_dma_pkg.cpp"
#include <sys/ioctl.h>

//#if defined (AXIDMA_TIMING)
//#include <stdio.h>
//#endif

#define BUFFER_SIZE ((6 * 1024 * 1024) / 8) //6 MBytes using uint64_t data type
//#define BUFFER_SIZE 1024

/** \brief Axidma Abstract class. Low level axi dma class controller.
 *
 * This class manages the registers of AXIDMA controller that are mapped on memory. There are functions to configure both axidma
 * channels (MM2S and S2MM) and to program the transfers.
 */
class Axidma_k {
    protected:
		const unsigned int MAX_WRITE_TRANSFER_LENGTH_BYTES;
		const unsigned int MAX_READ_TRANSFER_LENGTH_BYTES;
		const unsigned int MIN_READ_TRANSFER_LENGTH_BYTES;

		struct dma_proxy_channel_interface {
			uint64_t buffer[BUFFER_SIZE];
			enum proxy_status { PROXY_NO_ERROR = 0, PROXY_BUSY = 1, PROXY_TIMEOUT = 2, PROXY_ERROR = 3 } status;
			unsigned int length;
		};

		int tx_proxy_fd, rx_proxy_fd;
        dma_proxy_channel_interface* mm2s_channel;
        dma_proxy_channel_interface* s2mm_channel;

		uint64_t op_mode_ctrl_word;
		axi_parameters::axidma_transfer_mode operation_mode;
        unsigned int read_transfer_length_bytes;
        unsigned int read_transfer_length_words;
//        unsigned int write_transfer_length_bytes;
//        unsigned int write_transfer_length_words;

    public:

        /** \brief Constructor
         *
         *  @param axidma_addr_offset set the memory pointer where the AXIDMA module is mapped
         *  @param source_addr_offset set the memory pointer where data are read from MM2S channel
         *  @param destination_addr_offset the memory pointer where data are written by S2MM channel
         */
        Axidma_k(void);

        /** \brief Destructor
         */
        ~Axidma_k(void);

        /** \brief AXIDMA channels initialization
         *
         *  @param trans_read_length_bytes set the S2MM transfer length in bytes
         */
        bool init(axi_parameters::axidma_transfer_mode mode);

        /** \brief Reset both AXIDMA channels
         */
        void reset(void);

        /** \brief Stop both AXIDMA channels
         */
        void stop(void);

        /** \brief Read data that S2MM channel has written
         *
         *  @param data vector pointer to read data will be copied from S2MM channel
         *  @return number of read bytes from the actual S2MM tranfer
         */
        unsigned int read(std::vector<uint64_t> *data, axi_parameters::axidma_buffer_mode buffer_mode);

        /** \brief Write data to MM2S channel
         *
         *  @param data vector that will be write to MM2S channel
         *  @return number of written bytes from the actual MM2S tranfer
         */
        //unsigned int write(std::vector<uint64_t> *data);
        unsigned int write(const std::vector<uint64_t> * data, axi_parameters::axidma_buffer_mode buffer_mode);
};
#endif
