#!/bin/sh

RAW_VALUE=$(cat /sys/devices/platform/p6-spi.2/spi2.0/vbat)
MILLIVOLTS_VALUE=$(( ($RAW_VALUE * 4250) / 1023 ))

echo "ADC raw value: ${RAW_VALUE}"
echo "Millivolts: ${MILLIVOLTS_VALUE}"

