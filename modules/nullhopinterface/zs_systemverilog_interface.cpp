
/*
 * zs_systemverilog_interface.cpp
 *
 *  Created on: Oct 10, 2016
 *      Author: asa
 */
#define RTL_MODE


#include "zs_driver.hpp"
#include  "npp_log_utilities.cpp"
#include <iostream>
#include <stdexcept>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "svdpi.h"


zs_driver driver;
std::vector<int> input_image;
int classification_result = -1;
extern "C" int simulation_stop();

void process_exception(std::exception const& exc) {
   log_utilities::error("Exception in zs_systemverilog_interface\n");
   log_utilities::error("%s\n", exc.what());
   log_utilities::error("Simulation terminated by sw interface");

   simulation_stop();

}

//Implemented as task in sv, so needs to return 0
extern "C" int setup_zs_driver(const char* filename) {
   log_utilities::medium("Calling driver initialization...", filename);
   input_image.reserve(0);
   try {
      std::string filename_string(filename);
      driver = zs_driver(filename_string);
      log_utilities::medium("Driver initialized");
   } catch (std::exception const& exc) {

      process_exception(exc);
   }
   return (0);
}
//Implemented as task in sv, so needs to return 0
extern "C" int append_pixel_to_image(const int pixel) {
   try {
      input_image.push_back(pixel);
   } catch (std::exception const& exc) {

      process_exception(exc);
   }
   return (0);
}
//Implemented as task in sv, so needs to return 0
extern "C" int classify() {
   log_utilities::medium("Starting CNN Processing...");

   try {
      log_utilities::debug("input_image.size() %d",input_image.size() );
      classification_result = driver.classify_image(input_image.data());
      input_image.clear();
      input_image.reserve(0);
   } catch (std::exception const& exc) {
      process_exception(exc);
   }
   return (0); // it is a task in SW so must return 0
}

extern "C" int get_classification() {
   return (classification_result); // it is a task in SW so must return 0
}




//Implemented as task in sv, so needs to return 0
extern "C" int send_word_to_sw(const long int word) {
   try {
      driver.backend_if.append_new_rtl_word(word);
   } catch (std::exception const& exc) {
      process_exception(exc);
   }
   return (0);
}

