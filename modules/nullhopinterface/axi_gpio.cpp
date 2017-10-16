/*
 * axi_gpio.cpp
 *
 *  Created on: Mar 8, 2017
 *      Author: arios
 */

#include "axi_gpio.hpp"

Axigpio::Axigpio(int gl_gpio_base)
{
	this->gl_gpio_base = gl_gpio_base;
	this->nchannel = open_gpio_channel();
}

Axigpio::~Axigpio()
{
	close_gpio_channel();
}

int Axigpio::open_gpio_channel(/*int gpio_base*/)
{
	char gpio_nchan_file[128];
	int gpio_nchan_fd;
	int gpio_max;
	int nchannel;
	char nchannel_str[5];
	char *cptr;
	int c;
	char channel_str[5];

	char *gpio_export_file = "/sys/class/gpio/export";
	int export_fd=0;

	/* Check how many channels the GPIO chip has */
	sprintf(gpio_nchan_file, "%s/gpiochip%d/ngpio", GPIO_ROOT, gl_gpio_base);
	gpio_nchan_fd = open(gpio_nchan_file, O_RDONLY);
	if (gpio_nchan_fd < 0) {
		fprintf(stderr, "Failed to open %s: %s\n", gpio_nchan_file, strerror(errno));
		return -1;
	}
	read(gpio_nchan_fd, nchannel_str, sizeof(nchannel_str));
	close(gpio_nchan_fd);
	nchannel=(int)strtoul(nchannel_str, &cptr, 0);
	if (cptr == nchannel_str) {
		fprintf(stderr, "Failed to change %s into GPIO channel number\n", nchannel_str);
		exit(1);
	}

	/* Open files for each GPIO channel */
	export_fd=open(gpio_export_file, O_WRONLY);
	if (export_fd < 0) {
		fprintf(stderr, "Cannot open GPIO to export %d\n", gl_gpio_base);
		return -1;
	}

	gpio_max = gl_gpio_base + nchannel;
	for(c = gl_gpio_base; c < gpio_max; c++) {
		sprintf(channel_str, "%d", c);
		write(export_fd, channel_str, (strlen(channel_str)+1));
	}
	close(export_fd);
	return nchannel;
}

int Axigpio::close_gpio_channel(/*int gpio_base*/)
{
	char gpio_nchan_file[128];
	int gpio_nchan_fd;
	int gpio_max;
	int nchannel;
	char nchannel_str[5];
	char *cptr;
	int c;
	char channel_str[5];

	char *gpio_unexport_file = "/sys/class/gpio/unexport";
	int unexport_fd=0;

	/* Check how many channels the GPIO chip has */
	sprintf(gpio_nchan_file, "%s/gpiochip%d/ngpio", GPIO_ROOT, gl_gpio_base);
	gpio_nchan_fd = open(gpio_nchan_file, O_RDONLY);
	if (gpio_nchan_fd < 0) {
		fprintf(stderr, "Failed to open %s: %s\n", gpio_nchan_file, strerror(errno));
		return -1;
	}
	read(gpio_nchan_fd, nchannel_str, sizeof(nchannel_str));
	close(gpio_nchan_fd);
	nchannel=(int)strtoul(nchannel_str, &cptr, 0);
	if (cptr == nchannel_str) {
		fprintf(stderr, "Failed to change %s into GPIO channel number\n", nchannel_str);
		exit(1);
	}

	/* Close opened files for each GPIO channel */
	unexport_fd=open(gpio_unexport_file, O_WRONLY);
	if (unexport_fd < 0) {
		fprintf(stderr, "Cannot close GPIO by writing unexport %d\n", gl_gpio_base);
		return -1;
	}

	gpio_max = gl_gpio_base + nchannel;
	for(c = gl_gpio_base; c < gpio_max; c++) {
		sprintf(channel_str, "%d", c);
		write(unexport_fd, channel_str, (strlen(channel_str)+1));
	}
	close(unexport_fd);
	return 0;
}

int Axigpio::set_gpio_direction(/*int gpio_base,*/ /*int nchannel,*/ char *direction)
{
	char gpio_dir_file[128];
	int direction_fd=0;
	int gpio_max;
	int c;

	gpio_max = gl_gpio_base + nchannel;
	for(c = gl_gpio_base; c < gpio_max; c++) {
		sprintf(gpio_dir_file, "/sys/class/gpio/gpio%d/direction",c);
		direction_fd=open(gpio_dir_file, O_RDWR);
		if (direction_fd < 0) {
			fprintf(stderr, "Cannot open the direction file for GPIO %d\n", c);
			return 1;
		}
		write(direction_fd, direction, (strlen(direction)+1));
		close(direction_fd);
	}
	return 0;
}

int Axigpio::set_gpio_value(/*int gpio_base,*/ /*int nchannel,*/ int value)
{
	char gpio_val_file[128];
	int val_fd=0;
	int gpio_max;
	char val_str[2];
	int c;

	gpio_max = gl_gpio_base + nchannel;

	for(c = gl_gpio_base; c < gpio_max; c++) {
		sprintf(gpio_val_file, "/sys/class/gpio/gpio%d/value",c);
		val_fd=open(gpio_val_file, O_RDWR);
		if (val_fd < 0) {
			fprintf(stderr, "Cannot open the value file of GPIO %d\n", c);
			return -1;
		}
		sprintf(val_str,"%d", (value & 1));
		write(val_fd, val_str, sizeof(val_str));
		close(val_fd);
		value >>= 1;
	}
	return 0;
}


int Axigpio::get_gpio_value(/*int gpio_base,*/ /*int nchannel*/)
{
	char gpio_val_file[128];
	int val_fd=0;
	int gpio_max;
	char val_str[2];
	char *cptr;
	int value = 0;
	int c;

	gpio_max = gl_gpio_base + nchannel;

	for(c = gpio_max-1; c >= gl_gpio_base; c--)
	{
		sprintf(gpio_val_file, "/sys/class/gpio/gpio%d/value",c);
		val_fd=open(gpio_val_file, O_RDWR);
		if (val_fd < 0) {
			fprintf(stderr, "Cannot open GPIO to export %d\n", c);
			return -1;
		}
		read(val_fd, val_str, sizeof(val_str));
		value <<= 1;
		value += (int)strtoul(val_str, &cptr, 0);
		if (cptr == optarg) {
			fprintf(stderr, "Failed to change %s into integer", val_str);
		}
		close(val_fd);
	}
	return value;
}
