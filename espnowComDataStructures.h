#ifndef MYESPNOW_DATASTRUCT_H
#define MYESPNOW_DATASTRUCT_H

#include <stdint.h>

#define ESPNOW_NAME_LEN 10
#define ESPNOWCOM_MAX_DATA_LEN 255

// strcutres for send / recv events
typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    int len;
    void *send_data;
} espnowCom_sendEvent;

typedef struct {
    bool broadcast;
    uint8_t mac[ESP_NOW_ETH_ALEN];
    int len;
    void *recv_data;
} espnowCom_recvEvent;


typedef enum{
    espnowCom_DataType_Mgmt
}espnowCom_DataType;

typedef struct{
    uint8_t type;
    uint8_t data[];
}espnowCom_DataStruct_Base;

#define ESPNOWCOM_PAYLOADLEN 4
typedef struct{
    uint8_t type;
    uint8_t mgmt_type;
    uint8_t mgmt_count;
    uint8_t payload[ESPNOWCOM_PAYLOADLEN];
    char name[10];
}espnowCom_DataStruct_Mgmt;

// enum mgmt_type
typedef enum{
    espnowCom_MGMT_TYPE_SYN,        // sent by master to discover devices
    espnowCom_MGMT_TYPE_SYN_ACK,    // sent by slave to connect
    espnowCom_MGMT_TYPE_ACK,        // sent by master after connect
    espnowCom_MGMT_TYPE_PING,
}espnowCom_MGMT_TYPE;   

#endif