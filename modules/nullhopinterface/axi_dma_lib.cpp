#ifndef __AXIDMA_CPP__
#define __AXIDMA_CPP__
#include "axi_dma_lib.hpp"

Axidma::Axidma(unsigned int axidma_addr_offset, unsigned int source_addr_offset, unsigned int destination_addr_offset) :
        MM2S_CONTROL_REGISTER(0x00), MM2S_STATUS_REGISTER(0x04), MM2S_START_ADDRESS(0x18), MM2S_LENGTH(0x28), S2MM_CONTROL_REGISTER(0x30),
		S2MM_STATUS_REGISTER(0x34), S2MM_DESTINATION_ADDRESS(0x48), S2MM_LENGTH(0x58), AXIDMA_ADDR_OFFSET(axidma_addr_offset),
		DESTINATION_ADDR_OFFSET_0(destination_addr_offset), SOURCE_ADDR_OFFSET_0(source_addr_offset),
		DESTINATION_ADDR_OFFSET_1(DESTINATION_ADDR_OFFSET_0+AXIDMA_MEMORY_MAPPING_READ_SIZE_DEFINE), SOURCE_ADDR_OFFSET_1(SOURCE_ADDR_OFFSET_0+AXIDMA_MEMORY_MAPPING_WRITE_SIZE_DEFINE),
		RUNNING(0x00000000), HALTED(0x00000001), IDLE(0x00000002), SGINCLD(0x00000008),
		DMAINTERR(0x00000010), DMASLVERR(0x00000020), DMADECERR(0x00000040), SGINTERR(0x00000100), SGSLVERR(0x00000200), SGDECERR(0x00000400),
		IOC_IRQ(0x00001000), DLY_IRQ(0x00002000), ERR_IRQ(0x00004000),
		MIN_READ_TRANSFER_LENGTH_BYTES((unsigned int) pow(2, 4)), MAX_READ_TRANSFER_LENGTH_BYTES((unsigned int) pow(2, 23)),
		MAX_WRITE_TRANSFER_LENGTH_BYTES((unsigned int) pow(2, 23))
{

	axidma_channel_timeout_us = 5000 * 1000; //50 ms timeout
	read_transfer_length_bytes = axi_parameters::DEFAULT_AXI_READ_TRANSFER_LENGTH_BYTES;
	read_transfer_length_words = read_transfer_length_bytes / sizeof(uint64_t);
	write_transfer_length_bytes = axi_parameters::DEFAULT_AXI_WRITE_TRANSFER_LENGTH_BYTES;
	write_transfer_length_words = write_transfer_length_bytes / sizeof(uint64_t); //128 * 256; //256000;

	int whole_memory_pointer = open("/dev/mem", O_RDWR | O_SYNC); // Open /dev/mem which represents the whole physical memory
    axidma_map_addr = (unsigned int *) mmap(NULL, 65535, PROT_READ | PROT_WRITE,
    					MAP_SHARED, whole_memory_pointer, AXIDMA_ADDR_OFFSET); // Memory map AXI Lite register block

    destination_addr[0] = (uint64_t*) mmap(NULL, AXIDMA_MEMORY_MAPPING_READ_SIZE_DEFINE, PROT_READ | PROT_WRITE,
    					MAP_SHARED, whole_memory_pointer, DESTINATION_ADDR_OFFSET_0); // Memory map destination address

    destination_addr[1] = (uint64_t*) mmap(NULL, AXIDMA_MEMORY_MAPPING_READ_SIZE_DEFINE, PROT_READ | PROT_WRITE,
						MAP_SHARED, whole_memory_pointer, DESTINATION_ADDR_OFFSET_1); // Memory map destination address

    source_addr[0] = (uint64_t*) mmap(NULL, AXIDMA_MEMORY_MAPPING_WRITE_SIZE_DEFINE, PROT_READ | PROT_WRITE,
    				MAP_SHARED, whole_memory_pointer, SOURCE_ADDR_OFFSET_0); // Memory map source address

    source_addr[1] = (uint64_t*) mmap(NULL, AXIDMA_MEMORY_MAPPING_WRITE_SIZE_DEFINE, PROT_READ | PROT_WRITE,
					MAP_SHARED, whole_memory_pointer, SOURCE_ADDR_OFFSET_1); // Memory map source address

    op_mode_ctrl_word = 0;
    operation_mode = axi_parameters::partial;

#if defined (AXIDMA_TIMING)
    axidma_write_timing_file = fopen ("axidma_write_timing.log", "w");
    fclose(axidma_write_timing_file);
    axidma_read_timing_file = fopen ("axidma_read_timing.log", "w");
    fclose(axidma_read_timing_file);
#endif
}

Axidma::~Axidma(void) {
    munmap(axidma_map_addr, 65535);
    munmap(source_addr, AXIDMA_MEMORY_MAPPING_WRITE_SIZE_DEFINE);
    munmap(destination_addr, AXIDMA_MEMORY_MAPPING_READ_SIZE_DEFINE);
}

bool Axidma::init(axi_parameters::axidma_transfer_mode mode)
{
	log_utilities::debug("Initializing axidma controller");
    if (read_transfer_length_bytes > MAX_READ_TRANSFER_LENGTH_BYTES) {
        log_utilities::error("Error: The maximum read transfer length is %d bytes", MAX_READ_TRANSFER_LENGTH_BYTES);
        return (false);
    }

    if (read_transfer_length_bytes < MIN_READ_TRANSFER_LENGTH_BYTES) {
        log_utilities::error("Error: The minimum read transfer length is %d bytes", MIN_READ_TRANSFER_LENGTH_BYTES);
        return (false);
    }

    if(write_transfer_length_bytes > MAX_WRITE_TRANSFER_LENGTH_BYTES)
    {
    	log_utilities::error("Error: The maximum write transfer length is %d bytes", MAX_WRITE_TRANSFER_LENGTH_BYTES);
		return (false);
    }

    log_utilities::high("Initializing ZS_axidma using %d bytes as read transfer length", read_transfer_length_bytes);
	log_utilities::high("Initializing ZS_axidma using %d bytes as write transfer length", write_transfer_length_bytes);

    //After INIT we send to the HW mm2s2zs module the size of the burst for reading ops
    log_utilities::debug("Starting MM2S engine");
    set_dma_register_value(MM2S_START_ADDRESS, SOURCE_ADDR_OFFSET_0);
	set_dma_register_value(MM2S_CONTROL_REGISTER, 0xf001);  //IRQ disable  	(0xf001 for enable IRQ)
	std::vector<uint64_t> burst_size_vector;

	op_mode_ctrl_word = ((uint64_t) 1 & 0x1) << 62;
	operation_mode = mode;

    switch(operation_mode)
    {
    	case axi_parameters::partial:
    		log_utilities::debug("Configuring the axidma transfer mode as PARTIAL");
    		op_mode_ctrl_word |= ((uint64_t) (1) & 0x1) << 61;
    		op_mode_ctrl_word |= ((uint64_t) read_transfer_length_words & 0x1FFFFFFFFFFFFFFF);
		break;

    	case axi_parameters::completed:
    		log_utilities::debug("Configuring the axidma transfer mode as COMPLETED");
    		op_mode_ctrl_word |= ((uint64_t) (0) & 0x1) << 61;
    		op_mode_ctrl_word |= ((uint64_t) read_transfer_length_words & 0x1FFFFFFFFFFFFFFF);
		break;
    }

    log_utilities::debug("Writing axidma transfer mode control word: 0x%llx", op_mode_ctrl_word);
    burst_size_vector.push_back(op_mode_ctrl_word);
    write(&burst_size_vector, axi_parameters::single_B);

    /*int real_read_transfer_length_bytes = 1024;
    set_dma_register_value(S2MM_LENGTH, real_read_transfer_length_bytes);*/
    //set_dma_register_value(S2MM_DESTINATION_ADDRESS, DESTINATION_ADDR_OFFSET_0);
    log_utilities::debug("Starting S2MM engine");
    set_dma_register_value(S2MM_CONTROL_REGISTER, 0xf001);	//IRQ disable	(0xf001 for enable IRQ)

    /*unsigned long wait_to_init = 1000*1000;
     log_utilities::medium("Waiting %d us to initialize the axidma engine", wait_to_init);
     usleep(wait_to_init);*/

    return (true);
}

void Axidma::set_dma_register_value(int register_offset, unsigned int value) {
    axidma_map_addr[register_offset >> 2] = value;
}

unsigned int Axidma::get_dma_register_value(int register_offset) {
    return (axidma_map_addr[register_offset >> 2]);
}

bool Axidma::check_mm2s_status(unsigned int chk_status) {
    unsigned int status = get_dma_register_value(MM2S_STATUS_REGISTER);
    if (chk_status == RUNNING && (status & HALTED) == 0)
        return (true);
    if (status & chk_status)
        return (true);
    return (false);
}

bool Axidma::check_s2mm_status(unsigned int chk_status) {
    unsigned int status = get_dma_register_value(S2MM_STATUS_REGISTER);
    if (chk_status == RUNNING && (status & HALTED) == 0)
        return (true);
    if (status & chk_status)
        return (true);
    return (false);
}

void Axidma::reset(void) {
    set_dma_register_value(S2MM_CONTROL_REGISTER, 4);
    set_dma_register_value(MM2S_CONTROL_REGISTER, 4);
}

void Axidma::stop(void) {
    set_dma_register_value(S2MM_CONTROL_REGISTER, 0);
    set_dma_register_value(MM2S_CONTROL_REGISTER, 0);
}

unsigned int Axidma::write(const std::vector<uint64_t> * data, axi_parameters::axidma_buffer_mode buffer_mode) {

	unsigned int num_bytes = data->size() * sizeof(uint64_t);

	switch(operation_mode)
	{
		case (axi_parameters::partial):
			log_utilities::high("Write function called: write_transfer_length = data_vector_size: %d", data->size());
			log_utilities::high("Only single buffer mode is permitted for write operations using PARTIAL mode.");
			std::copy(data->begin(), data->end(), source_addr[0]);
			if ((num_bytes >= 8) && (num_bytes <= MAX_WRITE_TRANSFER_LENGTH_BYTES))
			{
				log_utilities::debug("Launching write operation: %d bytes, %d words.", num_bytes, data->size());
				set_dma_register_value(MM2S_LENGTH, num_bytes);
				log_utilities::debug("Write operation launched");
				mm2s_sync();
			}
			else
			{
				throw std::runtime_error("Write data on AXI bus failed in transfer size");
				return (-1);
			}
		break;

		case (axi_parameters::completed):
			log_utilities::high("Write function called for write a vector of %d words (%d bytes) using write_transfer_length_bytes of %d",
					data->size(), data->size()*sizeof(uint64_t), write_transfer_length_bytes);
			auto start_position = data->begin();

			//  auto end_position = std::min(start_position + write_transfer_length_words, data->end());
			//uint64_t operating_mode;
			auto end_position = start_position + write_transfer_length_words;

			if (end_position > data->end()) {
				end_position = data->end();
				//operating_mode = operating_mode_partial;
				//log_utilities::debug("AXI zs2s2mm read mode: partial");

			} /*else {
				//operating_mode = operating_mode_complete;
				log_utilities::debug("AXI zs2s2mm read mode: full");
			}*/

			uint32_t write_size_words = (end_position - start_position);	// + 1);
			uint32_t write_size_bytes = write_size_words * sizeof(uint64_t);
			bool continue_write = true;
			//log_utilities::debug("Calculated write_size_words %d and write_size_bytes %d", write_size_words, write_size_bytes);

			if(buffer_mode == axi_parameters::double_B)
			{
				log_utilities::debug("Write operation using DOUBLE_B: source_addr[0]");
				std::copy(start_position, end_position, source_addr[0]);
			}
			//source_addr[(uint64_t) write_size_words - 1] = operating_mode;
			unsigned int active_source = SOURCE_ADDR_OFFSET_0;

			do {
				if(buffer_mode == axi_parameters::single_B)
				{
					log_utilities::debug("Write operation using SINGLE_B: source_addr[0]");
					std::copy(start_position, end_position, source_addr[0]);
				}
				//printf("active_source: %u\n", active_source);
				set_dma_register_value(MM2S_START_ADDRESS, active_source);
				set_dma_register_value(MM2S_LENGTH, write_size_bytes);

				log_utilities::debug("%d bytes copied - range %d to %d", write_size_bytes, start_position - data->begin(),
						end_position - data->begin());

				start_position = end_position;
				end_position = start_position + write_transfer_length_words;
				if (end_position > data->end()) {
					end_position = data->end();
					//operating_mode = operating_mode_partial;
					//log_utilities::debug("AXI zs2s2mm read mode: partial");
				} /*else {
					//operating_mode = operating_mode_complete;
					log_utilities::debug("AXI zs2s2mm read mode: full");
				}*/

				if (start_position == data->end()) {
					continue_write = false;

				} else {
					write_size_words = (end_position - start_position);	// + 1);
					write_size_bytes = write_size_words * sizeof(uint64_t);

					if(buffer_mode == axi_parameters::double_B)
					{
						//swap source position
						if (active_source == SOURCE_ADDR_OFFSET_0) {
							log_utilities::debug("Write operation using DOUBLE_B: source_addr[1]");
							std::copy(start_position, end_position, source_addr[1]);
							//source_addr2[(uint64_t) write_size_words - 1] = operating_mode;
							active_source = SOURCE_ADDR_OFFSET_1;

						} else {
							log_utilities::debug("Write operation using DOUBLE_B: source_addr[0]");
							std::copy(start_position, end_position, source_addr[0]);
							//source_addr[(uint64_t) write_size_words - 1] = operating_mode;
							active_source = SOURCE_ADDR_OFFSET_0;
						}
					}
				}

				mm2s_sync();
				//usleep(500); //TODO remove. Only for test lookpback example

			} while (continue_write);
		break;
	}

    log_utilities::high("Write done");
    return (data->size());

}

/*unsigned int Axidma::write(std::vector<uint64_t> * data) {

    unsigned int numBytes = data->size() * sizeof(uint64_t);

#if defined (AXIDMA_TIMING)
    std::chrono::high_resolution_clock::time_point t1;
    std::chrono::high_resolution_clock::time_point t2;
    double duration;
    t1 = std::chrono::high_resolution_clock::now();
#endif
    std::copy(data->begin(), data->end(), source_addr);
#if defined (AXIDMA_TIMING)
    t2 = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast <std::chrono::nanoseconds> (t2 - t1).count();
    axidma_write_timing_file = fopen ("axidma_write_timing.log", "a");
    fprintf(axidma_write_timing_file,"%f us to copy data (%d bytes) to write memory buffer.\n", duration/1000, numBytes);
    fclose(axidma_write_timing_file);
#endif
    if ( (numBytes > 0) && (numBytes <= MAX_WRITE_TRANSFER_LENGTH_BYTES)) {

        int read_transfer_modifier = ceil((double) read_transfer_length_bytes / (double) MIN_READ_TRANSFER_LENGTH_BYTES);
        //source_addr[0] |= ((uint64_t) 1 & 0xFF) << 50;
        //source_addr[0] |= ((uint64_t) (read_transfer_modifier) & 0x1FFF) << 51; //We use 8Bytes words in the S2MM bus

#if defined (AXIDMA_TIMING)
        t1 = std::chrono::high_resolution_clock::now();
#endif
        log_utilities::debug("Launching write operation: %d bytes, %d words (vector size: %d). Read length modifier: %d",
                numBytes, numBytes / sizeof(uint64_t), data->size(), read_transfer_modifier);
        set_dma_register_value(MM2S_LENGTH, numBytes);
        log_utilities::debug("Write operation launched");
        mm2s_sync();
#if defined (AXIDMA_TIMING)
        t2 = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast <std::chrono::nanoseconds> (t2 - t1).count();
        axidma_write_timing_file = fopen ("axidma_write_timing.log", "a");
        fprintf(axidma_write_timing_file,"%f us to transfer (%d bytes) from write memory buffer to FPGA.\n", duration/1000, numBytes);
        fclose(axidma_write_timing_file);
#endif
        return (numBytes);
    } else {
        throw std::runtime_error("Write data on AXI bus over maximum transfer size");
    }
    return (0);
}*/

void Axidma::clear_mm2s_flags(void) {
    set_dma_register_value(MM2S_STATUS_REGISTER, 0x2); // Clear idle
    set_dma_register_value(MM2S_STATUS_REGISTER, 0x1000); // Clear IOC_Irq
}

unsigned int Axidma::read(std::vector<uint64_t> *data, axi_parameters::axidma_buffer_mode buffer_mode)
{
	//log_utilities::debug("num_word_per_read: %d - read_transfer_length_bytes %d", read_transfer_length_words,read_transfer_length_bytes);
	uint64_t* buffer_destination = destination_addr[0];
	unsigned int active_destination = DESTINATION_ADDR_OFFSET_0;

	log_utilities::debug("Launching read operation: %d bytes, %d words", read_transfer_length_bytes, read_transfer_length_bytes/sizeof(uint64_t));
	set_dma_register_value(S2MM_DESTINATION_ADDRESS, active_destination);
    set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);

    bool continue_read = true;

    do {
    	s2mm_sync();

        log_utilities::debug("Last data 0X%llx", buffer_destination[read_transfer_length_words - 1]);
        if (!((buffer_destination[read_transfer_length_words - 1] & 0x8000000000000000) == 0))
        {
            continue_read = false;
            log_utilities::high("Last keyword found");
        }

        if(buffer_mode == axi_parameters::double_B)
        {
			//data->insert(data->end(), buffer_destination, destination_addr + read_transfer_length_words);
			//select next destination
			if (active_destination == DESTINATION_ADDR_OFFSET_0) {
				log_utilities::debug("Dest = 0");
				if (continue_read == true) {
					set_dma_register_value(S2MM_DESTINATION_ADDRESS, DESTINATION_ADDR_OFFSET_1);
					set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);
				}
				data->insert(data->end(), buffer_destination, buffer_destination + read_transfer_length_words);

				active_destination = DESTINATION_ADDR_OFFSET_1;
				buffer_destination = destination_addr[1];
			}
			else
			{
				log_utilities::debug("Dest = 1");
				if (continue_read == true) {
					set_dma_register_value(S2MM_DESTINATION_ADDRESS, DESTINATION_ADDR_OFFSET_0);
					set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);
				}
				data->insert(data->end(), buffer_destination, buffer_destination + read_transfer_length_words);

				active_destination = DESTINATION_ADDR_OFFSET_0;
				buffer_destination = destination_addr[0];
			}
        }
        else if(buffer_mode == axi_parameters::single_B)
        {
        	log_utilities::debug("Coping read data from buffer_destination to vector data");
        	data->insert(data->end(), buffer_destination, buffer_destination + read_transfer_length_words);
        	if (continue_read == true)
        	{
				set_dma_register_value(S2MM_DESTINATION_ADDRESS, DESTINATION_ADDR_OFFSET_0);
				set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);
			}
        }

    } while (continue_read == true);

    log_utilities::high("Read from axi done");

    return (data->size()*sizeof(uint64_t));
}

void Axidma::clear_s2mm_flags(void) {
    set_dma_register_value(S2MM_STATUS_REGISTER, 0x2); // Clear idle
    set_dma_register_value(S2MM_STATUS_REGISTER, 0x1000); // Clear IOC_Irq
}

unsigned int Axidma::get_axidma_channel_timeout(void) {
    return (axidma_channel_timeout_us);
}

void Axidma::set_axidma_channel_timeout(unsigned int value) {
    axidma_channel_timeout_us = value;
}

unsigned int Axidma::get_read_transfer_length_bytes(void) {
    return (read_transfer_length_bytes);
}

Axidma_pool::Axidma_pool(unsigned int axidma_addr_offset, unsigned int source_addr_offset, unsigned int destination_addr_offset) :
        Axidma(axidma_addr_offset, source_addr_offset, destination_addr_offset) {
}

void Axidma_pool::mm2s_sync(void) throw (AXIDMA_timeout_exception) {
    log_utilities::debug("Sync mm2....");
    unsigned int status = get_dma_register_value(S2MM_STATUS_REGISTER);
    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
    while ( ! (check_mm2s_status(IOC_IRQ)) || ! (check_mm2s_status(IDLE))) {
        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        unsigned int time_span = std::chrono::duration_cast<time_span_us>(t2 - t1).count();
        if (time_span > axidma_channel_timeout_us) {
            throw axi_channel_timeout_excep;

        }
        status = get_dma_register_value(MM2S_STATUS_REGISTER);
        //usleep(1); //TODO remove?? The simple loopback test doen't work without this micro sleep
    }
    clear_mm2s_flags();
    log_utilities::debug("mm2s sync");
}

void Axidma_pool::s2mm_sync(void) throw (AXIDMA_timeout_exception) {
    log_utilities::debug("Sync s2mm....");
    unsigned int status = get_dma_register_value(S2MM_STATUS_REGISTER);
    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
    while ( ! (check_s2mm_status(IOC_IRQ)) || ! (check_s2mm_status(IDLE))) {
        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        unsigned int time_span = std::chrono::duration_cast<time_span_us>(t2 - t1).count();
        if (time_span > axidma_channel_timeout_us) {
            throw axi_channel_timeout_excep;
        }
        status = get_dma_register_value(S2MM_STATUS_REGISTER);
        //usleep(1); //TODO remove?? The simple loopback test doen't work without this micro sleep
    }
    clear_s2mm_flags();
    log_utilities::debug("s2mm sync");
}

Axidma_int::Axidma_int(unsigned int axidma_addr_offset, unsigned int source_addr_offset, unsigned int destination_addr_offset) :
        Axidma(axidma_addr_offset, source_addr_offset, destination_addr_offset) {

    IRQ_CHK = 0;
    IRQ_CLR = 1;

    mm2s_irq = open("/dev/uio0", O_RDWR);
    s2mm_irq = open("/dev/uio1", O_RDWR);

    FD_ZERO(&rfd_s2mm);
    FD_SET(0, &rfd_s2mm);
    FD_SET(s2mm_irq, &rfd_s2mm);
    FD_ZERO(&rfd_mm2s);
    FD_SET(0, &rfd_mm2s);
    FD_SET(mm2s_irq, &rfd_mm2s);

    mm2s_timeout.tv_usec = axidma_channel_timeout_us;
    s2mm_timeout.tv_usec = axidma_channel_timeout_us;
}

void Axidma_int::mm2s_sync(void) throw (AXIDMA_timeout_exception) {
    int rv_mm2s = ::select(mm2s_irq + 1, &rfd_mm2s, NULL, NULL, &mm2s_timeout);
    mm2s_timeout.tv_usec = axidma_channel_timeout_us;

    if (rv_mm2s == 0) {
        throw axi_channel_timeout_excep;
    } else {
        ::read(mm2s_irq, (void*) &IRQ_CHK, sizeof(int));
        clear_mm2s_flags();
        ::write(mm2s_irq, (void*) &IRQ_CLR, sizeof(int));
    }
}

void Axidma_int::s2mm_sync(void) throw (AXIDMA_timeout_exception) {
    int rv_s2mm = ::select(s2mm_irq + 1, &rfd_s2mm, NULL, NULL, &s2mm_timeout);
    s2mm_timeout.tv_usec = axidma_channel_timeout_us;

    if (rv_s2mm == 0) {
        throw axi_channel_timeout_excep;
    } else {
        ::read(s2mm_irq, (void*) &IRQ_CHK, sizeof(int));
        clear_s2mm_flags();
        ::write(s2mm_irq, (void*) &IRQ_CLR, sizeof(int));
    }
}

void Axidma::print_mm2s_status() {
    unsigned int status = get_dma_register_value(MM2S_STATUS_REGISTER);
    printf("Memory-mapped to stream status (0x%08x@0x%02x):", status, MM2S_STATUS_REGISTER);
    if (status & 0x00000001)
        printf(" halted");
    else
        printf(" running");
    if (status & 0x00000002)
        printf(" idle");
    if (status & 0x00000008)
        printf(" SGIncld");
    if (status & 0x00000010)
        printf(" DMAIntErr");
    if (status & 0x00000020)
        printf(" DMASlvErr");
    if (status & 0x00000040)
        printf(" DMADecErr");
    if (status & 0x00000100)
        printf(" SGIntErr");
    if (status & 0x00000200)
        printf(" SGSlvErr");
    if (status & 0x00000400)
        printf(" SGDecErr");
    if (status & 0x00001000)
        printf(" IOC_Irq");
    if (status & 0x00002000)
        printf(" Dly_Irq");
    if (status & 0x00004000)
        printf(" Err_Irq");
    printf("\n");
}

void Axidma::print_s2mm_status() {
    unsigned int status = get_dma_register_value(S2MM_STATUS_REGISTER);
    printf("Stream to memory-mapped status (0x%08x@0x%02x):", status, S2MM_STATUS_REGISTER);
    if (status & 0x00000001)
        printf(" halted");
    else
        printf(" running");
    if (status & 0x00000002)
        printf(" idle");
    if (status & 0x00000008)
        printf(" SGIncld");
    if (status & 0x00000010)
        printf(" DMAIntErr");
    if (status & 0x00000020)
        printf(" DMASlvErr");
    if (status & 0x00000040)
        printf(" DMADecErr");
    if (status & 0x00000100)
        printf(" SGIntErr");
    if (status & 0x00000200)
        printf(" SGSlvErr");
    if (status & 0x00000400)
        printf(" SGDecErr");
    if (status & 0x00001000)
        printf(" IOC_Irq");
    if (status & 0x00002000)
        printf(" Dly_Irq");
    if (status & 0x00004000)
        printf(" Err_Irq");
    printf("\n");
}

#endif
