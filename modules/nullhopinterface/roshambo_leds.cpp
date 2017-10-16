/*
 * roshambo_leds.cpp
 *
 *  Created on: Sep 25, 2017
 *      Author: arios
 */

#include "roshambo_leds.hpp"

Roshambo_leds::Roshambo_leds(void) : axigpio(AXIGPIO_LEDS_BASE)
{

}

Roshambo_leds::~Roshambo_leds(void)
{

}

void Roshambo_leds::represent_classification_result(int network_result)
{
	axigpio.set_gpio_direction("out");
	int led_pattern;

	switch(network_result)
	{
	case -1: //Unknown
		led_pattern = 0x0;
		break;
	case 3: //Background
		led_pattern = 0x0;
		break;
	case 0: //Paper
		led_pattern = 0x1;
		break;
	case 1: //Scissors
		led_pattern = 0x2;
		break;
	case 2: //Rock
		led_pattern = 0x4;
		break;
	}

	axigpio.set_gpio_value(led_pattern);
}
