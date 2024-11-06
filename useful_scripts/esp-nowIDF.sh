#!/bin/bash
# copy this file in the root folder of the project

source sdkconfig

if [ "$CONFIG_ESPNOWCOM_MASTERMODE" == "y" ]
then
    PORT="/dev/ttyESP-Now_Master"
else
    PORT="/dev/ttyESP-Now_Slave"
fi

echo idf.py $1 -p $PORT ${@:2}
idf.py $1 -p $PORT ${@:2}