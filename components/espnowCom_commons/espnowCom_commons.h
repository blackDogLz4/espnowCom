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
#define ESPNOWCOM_MAX_DATA_LEN 255

// Esp Now Datatypes
typedef enum{
    espnowCom_Data_Type_String,
    espnowCom_Data_Type_SimpleStruct,
}espnowCom_Data_Type;

// Data struct Default
typedef struct{
    uint8_t type;
    uint8_t data[];
}espnowCom_DataStruct;

// Data struct with float
typedef struct{
    uint8_t type;
    float some_float;
}espnowCom_DataStruct_SimpleStruct;

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
void espnowCom_send_string(char *str);
void espnowCom_send_float(float fl);

void espnowCom_deinit();



#endif // MYESPNOW_H 