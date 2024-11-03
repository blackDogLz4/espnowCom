#!/bin/bash
# copy this file in the root folder of the project

source sdkconfig

echo idf.py $1 -p $CONFIG_USB_PORT ${@:2}
idf.py $1 -p $CONFIG_USB_PORT ${@:2}