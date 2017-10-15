/*
 * roshambo_leds.hpp
 *
 *  Created on: Sep 25, 2017
 *      Author: arios
 */

#ifndef HW_LEDS_ROSHAMBO_ROSHAMBO_LEDS_HPP_
#define HW_LEDS_ROSHAMBO_ROSHAMBO_LEDS_HPP_

#include "axi_gpio.hpp"

#define AXIGPIO_LEDS_BASE 903

class Roshambo_leds {
private:
	Axigpio axigpio;    //!< Object used to control the hw leds

public:
	Roshambo_leds(void);

	~Roshambo_leds(void);

	void represent_classification_result(int network_result);

};


#endif /* HW_LEDS_ROSHAMBO_ROSHAMBO_LEDS_HPP_ */
