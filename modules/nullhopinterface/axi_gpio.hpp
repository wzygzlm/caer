/*
 * axi_gpio.hpp
 *
 *  Created on: Mar 8, 2017
 *      Author: arios
 */

#ifndef AXI_GPIO_HPP_
#define AXI_GPIO_HPP_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#define GPIO_ROOT "/sys/class/gpio"

/** \brief Axigpio class. Low level axi gpio class controller.
 *
 * This class manages the registers of AXIGPIO controller that are mapped on memory. There are functions to configure both axiqpio
 * ports per each AXIGPIO IP blocks.
 */
class Axigpio
{
    private:
        int gl_gpio_base;	//!< Base number that the Xilinx driver set to the gpio module
        int nchannel;		//!< Number ob channel of the gpio module

        /** \brief Open gpio channel using gl_gpio_base (private variable) number
         *
         *  @return nchannel (private variable) of the gpio module. -1 if an ERROR exists
         */
        int open_gpio_channel(/*int gpio_base*/);

        /** \brief Close de gpio channel
         *
         *  @return 0 if no ERROR. -1 if an ERROR exists
         */
        int close_gpio_channel(/*int gpio_base*/);

    public:
        /** \brief Constructor
         *
         *  @param gl_gpio_base set base number that the Xilinx driver set to the gpio module
         */
        Axigpio(int gl_gpio_base);

        /** \brief Destructor
         */
        ~Axigpio();

        /** \brief Configure the direction of the gpio port (in, out)
         *
         *  @param direction "in" or "out" value are possibles.
         *  @return 0 if no ERROR. -1 if an ERROR exists
         */
        int set_gpio_direction(/*int gpio_base,*/ /*int nchannel,*/ char *direction);

        /** \brief Write a specified data to the gpio port
         *
         *  @param value of the dato to write in the gpio port
         *  @return 0 if no ERROR. -1 if an ERROR exists
         */
        int set_gpio_value(/*int gpio_base,*/ /*int nchannel,*/ int value);

        /** \brief Read a specified data from the gpio port
         *
         *  @return value of the gpio port
         */
        int get_gpio_value(/*int gpio_base,*/ /*int nchannel*/);
};



#endif /* AXI_GPIO_HPP_ */
