#ifndef __ZS_DRIVER__
#define __ZS_DRIVER__

#include "zs_driver.hpp"

#include "npp_log_utilities.cpp"
#include <functional>
#include <numeric>
#include <iterator>
#include <vector>
#include <algorithm>
#include <string>
#include <exception>
#include <stdexcept>
#include "zs_std_func_pkg.cpp"
#include "omp.h"

#include <vector>
#include <functional>
#include <iostream>
#include <numeric>


zs_driver::zs_driver(const std::string network_file_name = "") {

    num_fc_layers = 0;
    num_cnn_layers = 0;
    total_num_layers = 0;
    total_num_processed_images = 0;

    performance_profiler = Npp_performance_profiler::getInstance();
    perf_network_loading = performance_profiler->add_label("Network loading");
    perf_input_image_conversion = performance_profiler->add_label("Input image conversion");
    perf_conv_layers = performance_profiler->add_label("Conv Layers total time");
    perf_fc_decompression = performance_profiler->add_label("Output decompression");
    perf_fc_layers = performance_profiler->add_label("FC Layers");
    perf_frame_total_time = performance_profiler->add_label("Frame total time");

    performance_profiler->start_checkpoint(perf_network_loading);

    log_utilities::none("Proceeding with network loading, network is: %s", network_file_name.c_str());

    if (network_file_name.empty() == false) {
        class_initialized = read_network_from_file(network_file_name); // Read a .net file containing network description and prepares arrays in memory
        monitor = zs_monitor(network_file_name);

        log_utilities::debug("Pre-loading config,biases and kernels for first layer...");
        load_config_biases_kernels(0, 0); // we start immediately to load config and weights for the first layer to save computational time
        log_utilities::debug("Pre-loading completed");
    } else {

        log_utilities::debug("No network file specified during driver initialization");
    }

    performance_profiler->stop_checkpoint(perf_network_loading);

}

//Note that this function expect input image multiplied by 256 since it is going to be shifted right by 16 position
int zs_driver::classify_image(caerFrameEventPacketConst frameIn) {

	//now convert l_image into int * l_image
	// only classify first frame, drop other for latency reasons
	caerFrameEventConst f = caerFrameEventPacketGetEventConst(frameIn, 0);

	int sizeX = caerFrameEventGetLengthX(f);
	int sizeY = caerFrameEventGetLengthY(f);

	int l_image[sizeX*sizeY];
	for(size_t i = 0; i<sizeX; i++){
		for(size_t j = 0; j<sizeY; j++){
			l_image[i*sizeY+j] = (caerFrameEventGetPixel(f,i,j) >> 8);
		}
	}


	performance_profiler->start_checkpoint(perf_frame_total_time);

    log_utilities::medium("*************************************\n\n");
    log_utilities::low("Starting classification of image %d", total_num_processed_images);
    monitor.classify_image(l_image);
    total_num_processed_images++;

    //Transform input image in ZS format and store it in class member first_layer_input
    //There is no return to avoid useless data movment and array initializations
    performance_profiler->start_checkpoint(perf_input_image_conversion);
    convert_input_image_int_to_short(l_image, first_layer_num_rows, first_layer_num_pixels);
    performance_profiler->stop_checkpoint(perf_input_image_conversion);

    uint16_t result = compute_network();
    performance_profiler->stop_checkpoint(perf_frame_total_time);
    return (result);
}

//Note that this function expect input image multiplied by 256 since it is going to be shifted right by 16 position
/*int zs_driver::classify_image(const int16_t* l_image) {
	performance_profiler->start_checkpoint(perf_frame_total_time);
    log_utilities::medium("*************************************\n\n");
    log_utilities::low("Starting classification of image %d", total_num_processed_images);
    monitor.classify_image(l_image);
    total_num_processed_images++;

    //Transform input image in ZS format and store it in class member first_layer_input
    //There is no return to avoid useless data movment and array initializations
    performance_profiler->start_checkpoint(perf_input_image_conversion);
    convert_input_image_short_to_short(l_image, first_layer_num_rows, first_layer_num_pixels);
    performance_profiler->stop_checkpoint(perf_input_image_conversion);

    uint16_t result = compute_network();

    performance_profiler->stop_checkpoint(perf_frame_total_time);
    performance_profiler->report();
    return (result);
}*/

uint16_t zs_driver::compute_network() {
    uint32_t classification_result;

#ifndef SOFTWARE_ONLY_MODE
    // First layer compute
    log_utilities::medium("Starting first layer computation on NHP...");

    performance_profiler->start_checkpoint(perf_conv_layers);
    try {
        //Next CNN layers compute
        for (uint16_t layer_idx = 0; layer_idx < num_cnn_layers; layer_idx++) {
            compute_cnn_layer(layer_idx);
            monitor.check_layer_activations(activations[layer_idx + 1], layer_idx);
        }
    } catch (std::exception& e) {

        log_utilities::error("**ERROR - Exception %s - Error in CNN computation, aborting...", e.what());
        return ( -1);
    }
    performance_profiler->stop_checkpoint(perf_conv_layers);
    log_utilities::medium("Convolutional layers completed, processing FC layers...");

    //FC layers
    //TODO In next future CNN and FC layers will derive from same base class, so we can alternate FC and CNN layers
    //TODO In current release FC layers can be only at network's end
    if (num_fc_layers > 0) {
    	performance_profiler->start_checkpoint(perf_fc_layers);
    	performance_profiler->start_checkpoint(perf_fc_decompression);
        zs_std::decompress_sm_image_as_linear_vector(activations[num_cnn_layers], zs_parameters::SPARSITY_MAP_WORD_NUM_BITS,
                fc_activations[0]);
        performance_profiler->stop_checkpoint(perf_fc_decompression);
        for (uint8_t fc_layer_idx = 0; fc_layer_idx < num_fc_layers; fc_layer_idx++) {
            log_utilities::medium("Starting FC layer %d...", fc_layer_idx);
            compute_fc_layer(fc_activations[fc_layer_idx], fc_activations[fc_layer_idx + 1], fc_layer_idx);

        }
        //We are not interested in probability distribution, so we return the position of the maximum that is the classification
        classification_result = std::distance(fc_activations[num_fc_layers].begin(),
                std::max_element(fc_activations[num_fc_layers].begin(), fc_activations[num_fc_layers].end()));

        performance_profiler->stop_checkpoint(perf_fc_layers);
    }

    monitor.check_classification(classification_result);
#else
    classification_result = monitor.get_monitor_classification();
#endif

    log_utilities::low("Classification result: %d", classification_result);

    return (classification_result);
}

//It assumes input is between 0 and 255 and needs to be normalized between 0 and 1
//The conversion is done in place directly on the class array to minimize data movement
//Code is optimized for computational speed rather than readability
inline void zs_driver::convert_input_image_int_to_short(const int* l_image, const uint16_t l_num_row,
        const uint32_t l_total_num_pixel) {

    uint32_t input_pixel_idx = 0, input_pixel_idx_incr = 1;

    log_utilities::debug("l_total_num_pixel %d", l_total_num_pixel);
    log_utilities::debug("axi_word_number %d", first_layer_num_axi_words);

    log_utilities::medium("Converting input image into internal format...");
    for (size_t output_pixel_idx = 0; output_pixel_idx < first_layer_num_axi_words; output_pixel_idx++) {

        activations[0][output_pixel_idx] = pixel_formatter.fast_2pixels_word_format(
                npp_std::int_to_short(l_image[input_pixel_idx]), npp_std::int_to_short(l_image[input_pixel_idx_incr]));

        input_pixel_idx = input_pixel_idx + 2;
        input_pixel_idx_incr = input_pixel_idx_incr + 2;

    }

    log_utilities::debug("Checking for odd number of rows...");
    if (first_layer_pixels_per_row_odd == true) {
        log_utilities::debug("Odd row written");
        //input_pixel_idx incremented in last iteration of the for so already pointing to right place
        activations[0][first_layer_num_axi_words] = pixel_formatter.fast_1pixel_word_format(
                npp_std::int_to_short(l_image[input_pixel_idx]));
    }
    activations[0][0] = pixel_formatter.set_new_row_flag(activations[0][0], 0); //for sure the first pixel starts a new row

    for (size_t row_idx = 0; row_idx < l_num_row - 1; row_idx++) {
        activations[0][first_layer_row_start_positions[row_idx]] = pixel_formatter.set_new_row_flag(
                activations[0][first_layer_row_start_positions[row_idx]], first_layer_row_start_positions_word_idx[row_idx]);

    }

    activations[0][activations[0].size() - 1] = pixel_formatter.format_word0((uint16_t) 1, (uint16_t) zs_parameters::REG_TYPE,
            (uint16_t) 1, (uint16_t) zs_address_space::config_image_load_done_pulse);

    log_utilities::debug("Conversion done.");
}

//It assumes input is between 0 and 255 and needs to be normalized between 0 and 1
//The conversion is done in place directly on the class array to minimize data movement
//Code is optimized for computational speed rather than readability
inline void zs_driver::convert_input_image_short_to_short(const int16_t* l_image, const uint16_t l_num_row,
        const uint32_t l_total_num_pixel) {

    uint32_t input_pixel_idx = 0, input_pixel_idx_incr = 1;

    log_utilities::debug("l_total_num_pixel %d", l_total_num_pixel);
    log_utilities::debug("axi_word_number %d", first_layer_num_axi_words);

    log_utilities::medium("Converting input image into internal format...");
    for (size_t output_pixel_idx = 0; output_pixel_idx < first_layer_num_axi_words; output_pixel_idx++) {

        activations[0][output_pixel_idx] = pixel_formatter.fast_2pixels_word_format(l_image[input_pixel_idx],
                l_image[input_pixel_idx_incr]);

        input_pixel_idx = input_pixel_idx + 2;
        input_pixel_idx_incr = input_pixel_idx_incr + 2;

    }

    log_utilities::debug("Checking for odd number of rows...");
    if (first_layer_pixels_per_row_odd == true) {
        log_utilities::debug("Odd row written");
        //input_pixel_idx incremented in last iteration of the for so already pointing to right place
        activations[0][first_layer_num_axi_words] = pixel_formatter.fast_1pixel_word_format(l_image[input_pixel_idx]);
    }
    activations[0][0] = pixel_formatter.set_new_row_flag(activations[0][0], 0); //for sure the first pixel starts a new row

    for (size_t row_idx = 0; row_idx < l_num_row - 1; row_idx++) {
        activations[0][first_layer_row_start_positions[row_idx]] = pixel_formatter.set_new_row_flag(
                activations[0][first_layer_row_start_positions[row_idx]], first_layer_row_start_positions_word_idx[row_idx]);

    }

    activations[0][activations[0].size() - 1] = pixel_formatter.format_word0((uint16_t) 1, (uint16_t) zs_parameters::REG_TYPE,
            (uint16_t) 1, (uint16_t) zs_address_space::config_image_load_done_pulse);

    log_utilities::debug("Conversion done.");
}

inline uint16_t zs_driver::get_input_activation_size(const uint16_t layer_idx) {
    const uint16_t input_size_kb = ( (activations[layer_idx].size() - 1)
            * (zs_axi_bits::VALUE_SIZE * zs_axi_bits::NUM_VALUES_INPUT_WORD)) / (8 * 1024);

    log_utilities::medium("Input activations size: %d KB", input_size_kb);
    return (input_size_kb);
}

inline bool zs_driver::get_multipass_image_in_memory(const uint16_t layer_idx, const uint16_t num_pass) {
    log_utilities::medium("Layer requires %d passes", num_pass);
    if (get_input_activation_size(layer_idx) < zs_parameters::IDP_MEMORY_SIZE_KB) {
        log_utilities::medium("Multipass layer operating with image in memory");
        return (true);
    } else {
        log_utilities::medium("Multipass layer operating without image in memory");
        return (false);
    }
}

inline void zs_driver::compute_cnn_layer(const uint16_t layer_idx) {
    const uint8_t num_pass = cnn_network[layer_idx].get_num_pass();
    log_utilities::medium("Starting layer %d...", layer_idx);
    if (num_pass == 1)
        compute_cnn_layer_singlepass(layer_idx);
    else
        compute_cnn_layer_multipass(layer_idx, num_pass);
}

inline void zs_driver::compute_cnn_layer_singlepass(const uint16_t layer_idx) {

    get_input_activation_size(layer_idx);
    log_utilities::medium("Layer is single pass");
    load_image(activations[layer_idx]);

    if (cnn_network[layer_idx].pooling_enabled == 0) {

        std::vector<uint64_t> activations_from_nhp;
        activations_from_nhp.reserve(cnn_network[layer_idx + 1].uncompressed_input_size * 2);
        backend_if.read(activations_from_nhp);

        load_kernel_config_biases_for_next_layer(layer_idx);

        zs_std::activations_reorder_no_pooling_relu_on(activations_from_nhp, activations[layer_idx + 1],
                cnn_network[layer_idx].num_output_rows, cnn_network[layer_idx].num_sm_output_rows,
                cnn_network[layer_idx].pre_sm_counter_max + 1);

    } else {
        backend_if.read(activations[layer_idx + 1]);

        load_kernel_config_biases_for_next_layer(layer_idx);
    }

    if (cnn_network[layer_idx].get_cnn_stride() > 1) {
        zs_std::activations_stride_shrink(activations[layer_idx + 1], cnn_network[layer_idx].get_cnn_stride());
    }
}

inline void zs_driver::compute_cnn_layer_multipass(const uint16_t layer_idx, const uint16_t num_pass) {

    const bool multipass_image_in_memory = get_multipass_image_in_memory(layer_idx, num_pass);
    std::vector<std::vector<uint64_t>> multipass_activations(num_pass, std::vector<uint64_t>(1, 0));

    for (size_t pass_idx = 0; pass_idx < num_pass; pass_idx++) {

        log_utilities::medium("Starting pass %d...", pass_idx);

        //For first pass image must always be loaded
        if (pass_idx == 0 || multipass_image_in_memory == false) {
            log_utilities::full("Loading image...");
            load_image(activations[layer_idx]);
        }

        if (cnn_network[layer_idx].pooling_enabled == 0) {
            log_utilities::full("Layer without pooling");
            std::vector<uint64_t> activations_from_nhp;

            backend_if.read(activations_from_nhp);
            log_utilities::debug("Read returned to controller");

            //Config/Kernels/Biases for first layer first pass are loaded at the end of previous pass
            if (pass_idx + 1 < num_pass) {
                log_utilities::debug("Setting image in memory value...");
                cnn_network[layer_idx].set_image_in_memory_for_pass(pass_idx + 1, multipass_image_in_memory);
                load_config_biases_kernels(layer_idx, pass_idx + 1);
            } else {
                //Preloads config for next layer
                load_kernel_config_biases_for_next_layer(layer_idx);
            }

            zs_std::activations_reorder_no_pooling_relu_on(activations_from_nhp, multipass_activations[pass_idx],
                    cnn_network[layer_idx].num_output_rows, cnn_network[layer_idx].num_sm_output_rows / num_pass,
                    cnn_network[layer_idx].pre_sm_counter_max + 1);

        } else {
            log_utilities::full("Layer with pooling");
            backend_if.read(multipass_activations[pass_idx]);

            //Config/Kernels/Biases for first layer first pass are loaded at the end of previous pass
            if (pass_idx + 1 < num_pass) {
                log_utilities::debug("Setting image in memory value...");
                cnn_network[layer_idx].set_image_in_memory_for_pass(pass_idx + 1, multipass_image_in_memory);
                load_config_biases_kernels(layer_idx, pass_idx + 1);
            } else {
                //Preloads config for next layer
                load_kernel_config_biases_for_next_layer(layer_idx);
            }
        }

    }

    //Multipass results merging
    zs_std::activations_multipass_merge(multipass_activations, activations[layer_idx + 1], cnn_network[layer_idx].num_sm_output,
            cnn_network[layer_idx].num_sm_per_channel_per_pass);

    //NHP currently supports only stride of 1, so in case of larger stride we need to compensate in SW
    if (cnn_network[layer_idx].get_cnn_stride() > 1) {
        zs_std::activations_stride_shrink(activations[layer_idx + 1], cnn_network[layer_idx].get_cnn_stride());
    }
    log_utilities::full("Conv Layer computation done");

}

inline void zs_driver::load_config_biases_kernels(const uint16_t layer_idx, const uint16_t pass_idx) {
    log_utilities::high("Starting loading of config, biases and kernels...");
    log_utilities::high("Config/Kernels/Biases for layer %d, writing %d KB of data", layer_idx,
            (cnn_network[layer_idx].get_load_array(pass_idx)->size() * sizeof(uint64_t)) / 1024);
    backend_if.write(cnn_network[layer_idx].get_load_array(pass_idx));
}

inline void zs_driver::load_image(const std::vector<uint64_t> &l_input) {
    const uint32_t size_kb = (l_input.size() * sizeof(uint64_t)) / 1024;

    log_utilities::high("Starting image load, number of words to send: %d, size: %d KB", l_input.size(), size_kb);
    backend_if.write( &l_input);
}

inline void zs_driver::load_kernel_config_biases_for_next_layer(const uint16_t layer_idx) {
    log_utilities::full("Loading next layer kernels...");
    const uint16_t next_layer_idx = layer_idx + 1;
    if (next_layer_idx < num_cnn_layers) {
        load_config_biases_kernels(next_layer_idx, 0);
    } else {
        load_config_biases_kernels(0, 0);
    }
}

//FC layers are currently computed in SW
//Notice that we are operating on both weight and pixel shifted left by MANTISSA_NUM_BITS, so the mult result is going to be shifted by 2 times this value
//In order to speedup the computation biases are read and shifted left by MANTISSA_NUM_BITS (thus realigned with pixels/weight mult result)
//and we just need to shift the result back after the computation
//TODO: Pooling currently not supported
inline void zs_driver::compute_fc_layer(const std::vector<int16_t> &l_input, std::vector<int16_t> &fc_output,
        const uint16_t layer_idx) {

    const uint32_t layer_num_output_channels = fc_network[layer_idx].num_output_channels;

    log_utilities::debug("FC Input vector size: %d", l_input.size());
    log_utilities::debug("FC Output vector size: %d", fc_output.size());
    log_utilities::debug("Expected output size: %d", layer_num_output_channels);

#pragma omp parallel for shared(l_input) num_threads(4) schedule(dynamic)
    for (size_t kernel_idx = 0; kernel_idx < layer_num_output_channels; kernel_idx++) {
        fc_output[kernel_idx] = std::inner_product(l_input.begin(), l_input.end(),
                fc_network[layer_idx].weights[kernel_idx].begin(), fc_network[layer_idx].biases[kernel_idx])
                / zs_parameters::MANTISSA_RESCALE_FACTOR;
    }

    if (fc_network[layer_idx].relu_enabled == 1) {
        for (size_t kernel_idx = 0; kernel_idx < layer_num_output_channels; kernel_idx++) {
            if (fc_output[kernel_idx] < 0) {
                fc_output[kernel_idx] = 0;

            }
        }
    }

    if (fc_network[layer_idx].pooling_enabled == 1) {
        log_utilities::error("Pooling in FC layer still not supported");
        throw std::invalid_argument("Failed attempt to read network file, impossible to proceed");
    }

}

inline bool zs_driver::read_network_from_file(std::string network_file_name) {
    log_utilities::full("Opening network file %s", network_file_name.c_str());
    FILE *l_net_file = fopen(network_file_name.c_str(), "r");

    if (l_net_file == NULL) {
        log_utilities::error("File opening failed");
        throw std::invalid_argument("Failed attempt to read network file, impossible to proceed");

    } else {
        log_utilities::debug("File opened successfully");

        //Read number of layers
        total_num_layers = npp_std::read_int_from_file(l_net_file);
        cnn_network.resize(total_num_layers);
        fc_network.resize(total_num_layers);
        log_utilities::full("Network structure initialized with %d layers", total_num_layers);

        //Create layers to be used
        num_cnn_layers = 0;
        num_fc_layers = 0;
        for (size_t layer_idx = 0; layer_idx < total_num_layers; layer_idx++) {
            const uint8_t layer_type = npp_std::read_int_from_file(l_net_file);

            if (layer_type == 1) { //The layer has to run on accelerator
                log_utilities::medium("Layer %d, type: CONV", layer_idx);
                cnn_network[num_cnn_layers] = zs_cnn_layer(layer_idx, l_net_file);
                num_cnn_layers++;
                activations.resize(num_cnn_layers + 1);
                activations[layer_idx].resize(0);
                activations[layer_idx].reserve(cnn_network[num_cnn_layers].uncompressed_input_size * 4);
            } else {
                log_utilities::medium("Layer %d, type: FC", layer_idx);
                fc_network[num_fc_layers] = zs_fc_layer(layer_idx, l_net_file);

                fc_activations.resize(num_fc_layers + 2);
                fc_activations[num_fc_layers].resize(fc_network[num_fc_layers].uncompressed_input_size);
                fc_activations[num_fc_layers + 1].resize(fc_network[num_fc_layers].num_output_channels);
                num_fc_layers++;

            }

        }

        cnn_network.resize(num_cnn_layers);
        fc_network.resize(num_fc_layers);

        fclose(l_net_file);
        log_utilities::medium("Number of layers - CNN: %d - FC: %d - Total: %d", num_cnn_layers, num_fc_layers, total_num_layers);
        log_utilities::full("All layer read, proceeding with first layer data structure preparation...");

        first_layer_num_pixels = cnn_network[0].get_uncompressed_input_image_num_pixels();
        first_layer_num_rows = cnn_network[0].get_input_num_rows();
        first_layer_pixels_per_row = cnn_network[0].get_pixels_per_row();
        first_layer_num_axi_words = first_layer_num_pixels >> 1;

        if (first_layer_pixels_per_row % 2 == 0)
            first_layer_pixels_per_row_odd = false;
        else
            first_layer_pixels_per_row_odd = true;

        activations[0].resize(first_layer_num_axi_words + (int) first_layer_pixels_per_row_odd + 1); //+1 for image load complete instruction

        for (size_t pixel_idx = 1; pixel_idx < first_layer_num_pixels; pixel_idx++) {
            if (pixel_idx % first_layer_pixels_per_row == 0) {
                first_layer_row_start_positions.push_back(pixel_idx >> 1);
                first_layer_row_start_positions_word_idx.push_back(pixel_idx % 2);
            }
        }

        log_utilities::debug("Network read from file done - Preparation completed");
        return (true);
    }
}

#endif
