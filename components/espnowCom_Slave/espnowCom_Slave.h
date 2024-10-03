#ifndef ESPNOWCOM_SLAVE
#define ESPNOWCOM_SLAVE

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

#endif