/* myespnow.h */

#ifndef MYESPNOW_H
#define MYESPNOW_H

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

#define ESPNOW_QUEUE_SIZE 30
#define ESPNOWCOM_MAX_DATA_LEN 255
#ifdef CONFIG_ESPNOWCOM_MASTERMODE
#define MASTER 1
#else
#define MASTER 0
#endif
// Data structures
// send event stores:
//          mac -- mac to send Data to
//          len -- len of the Data in Bytes Note Max. ESPNOWCOM_MAX_DATA_LEN 
typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    int len;
    void *send_data;
} espnowCom_sendEvent;

typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    int len;
    void *recv_data;
} espnowCom_recvEvent;


typedef enum {
    espnowCom_Sate_Connect,
    espnowCom_Sate_Run
}espnowCom_States;

//functions
int espnowCom_init();
void espnowCom_switchMode(espnowCom_States state);

// Send functions master
#ifdef CONFIG_ESPNOWCOM_MASTERMODE
    void espnowCom_send_string(char *str);
    void espnowCom_send_float(float fl);
// Send functions slave
#else
    void espnowCom_send_string(char *str);
    void espnowCom_send_float(float fl);
#endif
void espnowCom_deinit();



#endif // MYESPNOW_H 