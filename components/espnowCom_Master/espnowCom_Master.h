#ifndef ESPNOWCOM_MASTER
#define ESPNOWCOM_MASTER

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

#define MAX_SLAVES 1

int espnowCom_init();
int espnowCom_send(int slave, int type, void *data, int size);
int espnowCom_addRecv_cb(int type, void *cb_function (int, int, void*));
#endif