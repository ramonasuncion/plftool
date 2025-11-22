#!/bin/sh

if test "$#" -ne 1; then
	echo "Please specify PWM value (0-255)"
	exit 1
fi

echo "Press CTRL+C to exit."
while [ 1 ]; do echo $1 > /sys/devices/platform/p6-spi.2/spi2.0/baro_heater_pwm; done

