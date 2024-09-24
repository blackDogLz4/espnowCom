/* myespnow.h */

#ifndef MYESPNOW_H
#define MYESPNOW_H

#include "esp_log.h"
#define TAG "espnowCom_commons"

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

#define ESPNOW_QUEUE_SIZE 30

// Data structures
typedef struct {
    uint8_t message[10];
    int len;
} espnowCom_SendEvent;

typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    uint8_t message[10];
    int len;
} espnowCom_RecvEvent;

typedef enum {
    espnowCom_Sate_Connect,
    espnowCom_Sate_Run
}espnowCom_States;

//functions
int espnowCom_init();
void espnowCom_switchMode(espnowCom_States state);
void espnowCom_send(char *str);

void espnowCom_deinit();



#endif // MYESPNOW_H 