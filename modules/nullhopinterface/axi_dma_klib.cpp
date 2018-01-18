#ifndef __AXIDMA_CPP__
#define __AXIDMA_CPP__
#include "axi_dma_klib.hpp"

Axidma_k::Axidma_k(void) :
		MIN_READ_TRANSFER_LENGTH_BYTES((unsigned int) pow(2, 3)), MAX_READ_TRANSFER_LENGTH_BYTES(BUFFER_SIZE*sizeof(uint64_t)),
		MAX_WRITE_TRANSFER_LENGTH_BYTES(BUFFER_SIZE*sizeof(uint64_t))
{
	read_transfer_length_bytes = axi_parameters::DEFAULT_AXI_READ_TRANSFER_LENGTH_BYTES;
	read_transfer_length_words = read_transfer_length_bytes / sizeof(uint64_t);

    op_mode_ctrl_word = 0;
    operation_mode = axi_parameters::partial;

	tx_proxy_fd = open("/dev/dma_proxy_tx", O_RDWR);
	rx_proxy_fd = open("/dev/dma_proxy_rx", O_RDWR);
	if (tx_proxy_fd < 1 || rx_proxy_fd < 1)
	{
		log_utilities::error("Unable to open DMA proxy device file");
	}

	mm2s_channel = (struct dma_proxy_channel_interface *)mmap(NULL, sizeof(struct dma_proxy_channel_interface),
			PROT_READ | PROT_WRITE, MAP_SHARED, tx_proxy_fd, 0);
	
	s2mm_channel = (struct dma_proxy_channel_interface *)mmap(NULL, sizeof(struct dma_proxy_channel_interface),
				PROT_READ | PROT_WRITE, MAP_SHARED, rx_proxy_fd, 0);

	if ((mm2s_channel == MAP_FAILED) || (s2mm_channel == MAP_FAILED))
	{
		log_utilities::error("Failed to mmap");
	}

}

Axidma_k::~Axidma_k(void)
{
	munmap(mm2s_channel, sizeof(struct dma_proxy_channel_interface));
	munmap(s2mm_channel, sizeof(struct dma_proxy_channel_interface));
	close(tx_proxy_fd);
	close(rx_proxy_fd);
}

bool Axidma_k::init(axi_parameters::axidma_transfer_mode mode)
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

//    if(write_transfer_length_bytes > MAX_WRITE_TRANSFER_LENGTH_BYTES)
//    {
//    	log_utilities::error("Error: The maximum write transfer length is %d bytes", MAX_WRITE_TRANSFER_LENGTH_BYTES);
//		return (false);
//    }

    log_utilities::high("Initializing ZS_axidma using %d bytes as read transfer length", read_transfer_length_bytes);
//	log_utilities::high("Initializing ZS_axidma using %d bytes as write transfer length", write_transfer_length_bytes);

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

    return (true);
}

void Axidma_k::reset(void)
{

}

void Axidma_k::stop(void)
{

}

unsigned int Axidma_k::write(const std::vector<uint64_t> * data, axi_parameters::axidma_buffer_mode buffer_mode) {

	unsigned int num_bytes = data->size() * sizeof(uint64_t);
	int dummy;

	switch(operation_mode)
	{
		case (axi_parameters::partial):
			if ((num_bytes >= 8) && (num_bytes <= MAX_WRITE_TRANSFER_LENGTH_BYTES))
			{
				mm2s_channel->length = num_bytes;
				std::copy(data->begin(), data->end(), mm2s_channel->buffer);

//				log_utilities::debug("Launching write operation: %d bytes, %d words.", num_bytes, data->size());
//				log_utilities::debug("Print vector and mm2s_channel->buffer contents");
//				log_utilities::debug("Vector word: 0X%llx", data->data()[0]);
//				log_utilities::debug("Array word: 0X%llx", mm2s_channel->buffer[0]);
				/*log_utilities::debug("Array words: %d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d",
						mm2s_channel->buffer[15], mm2s_channel->buffer[14], mm2s_channel->buffer[13], mm2s_channel->buffer[12],
						mm2s_channel->buffer[11], mm2s_channel->buffer[10], mm2s_channel->buffer[9], mm2s_channel->buffer[8],
						mm2s_channel->buffer[7], mm2s_channel->buffer[6], mm2s_channel->buffer[5], mm2s_channel->buffer[4],
						mm2s_channel->buffer[3], mm2s_channel->buffer[2], mm2s_channel->buffer[1], mm2s_channel->buffer[0]);*/

				log_utilities::high("Write function called: write_transfer_length = data_vector_size: %d", data->size());
				log_utilities::high("Only single buffer mode is permitted for write operations using PARTIAL mode.");
				
				ioctl(tx_proxy_fd, 0, &dummy);
				
				if (mm2s_channel->status != 0)
				{
					log_utilities::error("Proxy tx transfer error");
				}
				else
				{
					log_utilities::debug("Write operation finished");
				}
			}
			else
			{
				throw std::runtime_error("Write data on AXI bus failed in transfer size");
				return (-1);
			}
		break;

		case (axi_parameters::completed):
//			log_utilities::high("Write function called for write a vector of %d words (%d bytes) using write_transfer_length_bytes of %d",
//					data->size(), data->size()*sizeof(uint64_t), write_transfer_length_bytes);
//			auto start_position = data->begin();
//
//			//  auto end_position = std::min(start_position + write_transfer_length_words, data->end());
//			//uint64_t operating_mode;
//			auto end_position = start_position + write_transfer_length_words;
//
//			if (end_position > data->end()) {
//				end_position = data->end();
//				//operating_mode = operating_mode_partial;
//				//log_utilities::debug("AXI zs2s2mm read mode: partial");
//
//			} /*else {
//				//operating_mode = operating_mode_complete;
//				log_utilities::debug("AXI zs2s2mm read mode: full");
//			}*/
//
//			uint32_t write_size_words = (end_position - start_position);	// + 1);
//			uint32_t write_size_bytes = write_size_words * sizeof(uint64_t);
//			bool continue_write = true;
//			//log_utilities::debug("Calculated write_size_words %d and write_size_bytes %d", write_size_words, write_size_bytes);
//
//			if(buffer_mode == axi_parameters::double_B)
//			{
//				log_utilities::debug("Write operation using DOUBLE_B: source_addr[0]");
//				std::copy(start_position, end_position, source_addr[0]);
//			}
//			//source_addr[(uint64_t) write_size_words - 1] = operating_mode;
//			unsigned int active_source = SOURCE_ADDR_OFFSET_0;
//
//			do {
//				if(buffer_mode == axi_parameters::single_B)
//				{
//					log_utilities::debug("Write operation using SINGLE_B: source_addr[0]");
//					std::copy(start_position, end_position, source_addr[0]);
//				}
//				//printf("active_source: %u\n", active_source);
//				set_dma_register_value(MM2S_START_ADDRESS, active_source);
//				set_dma_register_value(MM2S_LENGTH, write_size_bytes);
//
//				log_utilities::debug("%d bytes copied - range %d to %d", write_size_bytes, start_position - data->begin(),
//						end_position - data->begin());
//
//				start_position = end_position;
//				end_position = start_position + write_transfer_length_words;
//				if (end_position > data->end()) {
//					end_position = data->end();
//					//operating_mode = operating_mode_partial;
//					//log_utilities::debug("AXI zs2s2mm read mode: partial");
//				} /*else {
//					//operating_mode = operating_mode_complete;
//					log_utilities::debug("AXI zs2s2mm read mode: full");
//				}*/
//
//				if (start_position == data->end()) {
//					continue_write = false;
//
//				} else {
//					write_size_words = (end_position - start_position);	// + 1);
//					write_size_bytes = write_size_words * sizeof(uint64_t);
//
//					if(buffer_mode == axi_parameters::double_B)
//					{
//						//swap source position
//						if (active_source == SOURCE_ADDR_OFFSET_0) {
//							log_utilities::debug("Write operation using DOUBLE_B: source_addr[1]");
//							std::copy(start_position, end_position, source_addr[1]);
//							//source_addr2[(uint64_t) write_size_words - 1] = operating_mode;
//							active_source = SOURCE_ADDR_OFFSET_1;
//
//						} else {
//							log_utilities::debug("Write operation using DOUBLE_B: source_addr[0]");
//							std::copy(start_position, end_position, source_addr[0]);
//							//source_addr[(uint64_t) write_size_words - 1] = operating_mode;
//							active_source = SOURCE_ADDR_OFFSET_0;
//						}
//					}
//				}
//
//				mm2s_sync();
//				//usleep(500); //TODO remove. Only for test lookpback example
//
//			} while (continue_write);
		break;
	}

    log_utilities::high("Write done");
    return (num_bytes);

}

unsigned int Axidma_k::read(std::vector<uint64_t> *data, axi_parameters::axidma_buffer_mode buffer_mode)
{
	//log_utilities::debug("num_word_per_read: %d - read_transfer_length_bytes %d", read_transfer_length_words,read_transfer_length_bytes);
//	uint64_t* buffer_destination = destination_addr[0];
//	unsigned int active_destination = DESTINATION_ADDR_OFFSET_0;

	log_utilities::debug("Launching read operation: %d bytes, %d words", read_transfer_length_bytes, read_transfer_length_words);
//	set_dma_register_value(S2MM_DESTINATION_ADDRESS, active_destination);
//  set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);
	int dummy;
	s2mm_channel->length = read_transfer_length_bytes;
	ioctl(rx_proxy_fd, 0, &dummy);
	if (s2mm_channel->status != 0)
	{
		log_utilities::error("Proxy rx transfer error");
	}

    bool continue_read = true;

    do
    {
    	log_utilities::debug("Last data 0X%llx", s2mm_channel->buffer[read_transfer_length_words - 1]);
        if (!((s2mm_channel->buffer[read_transfer_length_words - 1] & 0x8000000000000000) == 0))
        {
            continue_read = false;
            log_utilities::high("Last keyword found");
        }

        if(buffer_mode == axi_parameters::double_B)
        {
//			//data->insert(data->end(), buffer_destination, destination_addr + read_transfer_length_words);
//			//select next destination
//			if (active_destination == DESTINATION_ADDR_OFFSET_0) {
//				log_utilities::debug("Dest = 0");
//				if (continue_read == true) {
//					set_dma_register_value(S2MM_DESTINATION_ADDRESS, DESTINATION_ADDR_OFFSET_1);
//					set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);
//				}
//				data->insert(data->end(), buffer_destination, buffer_destination + read_transfer_length_words);
//
//				active_destination = DESTINATION_ADDR_OFFSET_1;
//				buffer_destination = destination_addr[1];
//			}
//			else
//			{
//				log_utilities::debug("Dest = 1");
//				if (continue_read == true) {
//					set_dma_register_value(S2MM_DESTINATION_ADDRESS, DESTINATION_ADDR_OFFSET_0);
//					set_dma_register_value(S2MM_LENGTH, read_transfer_length_bytes);
//				}
//				data->insert(data->end(), buffer_destination, buffer_destination + read_transfer_length_words);
//
//				active_destination = DESTINATION_ADDR_OFFSET_0;
//				buffer_destination = destination_addr[0];
//			}
        	return 0;
        }
        else if(buffer_mode == axi_parameters::single_B)
        {
        	log_utilities::debug("Coping read data from buffer_destination to vector data");
        	data->insert(data->end(), s2mm_channel->buffer, s2mm_channel->buffer + read_transfer_length_words);
        	if (continue_read == true)
        	{
        		ioctl(rx_proxy_fd, 0, &dummy);
        		if (s2mm_channel->status != 0)
				{
					log_utilities::error("Proxy rx transfer error");
				}
			}
        }

    } while (continue_read == true);

    log_utilities::high("Read from axi done");

    return (data->size() * sizeof(uint64_t));
}
#endif
