#ifndef ESPNOWCOM_H
#define ESPNOWCOM_H

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "esp_random.h"
#include "espnowComDataStructures.h"

// set wifi Mode
#ifdef CONFIG_ESPNOWCOM_WIFI_MODE_STATION
    #define WIFI_MODE WIFI_MODE_STA
#else
    #define WIFI_MODE WIFI_MODE_SOFTAP
#endif

// long range
#ifdef CONFIG_ESPNOWCOM_ENABLE_LONG_RANGE
    #define ESPNOWCOM_LONGRANGE 1
#else
    #define ESPNOWCOM_LONGRANGE 0
#endif

#ifdef CONFIG_ESPNOWCOM_MASTERMODE

    // callback functions
    typedef void (*espnowCom_user_receive_cb) (int, int, void*, int);

    // main function
    int espnowCom_init();
    int espnowCom_send(int slave, int type, void *data, int size);
    int espnowCom_addRecv_cb(int type, espnowCom_user_receive_cb cb_function);


#elif CONFIG_ESPNOWCOM_SLAVEMODE

    // callback functions for slave
    typedef void (*espnowCom_user_receive_cb) (int, void*, int);

    // main functions
    int espnowCom_init();
    int espnowCom_send(int type, void *data, int size);
    int espnowCom_addRecv_cb(int type, espnowCom_user_receive_cb cb_function);

#endif


#endif