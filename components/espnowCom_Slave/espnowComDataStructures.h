#ifndef MYESPNOW_DATASTRUCT_H
#define MYESPNOW_DATASTRUCT_H

#include <stdint.h>

#define ESPNOW_QUEUE_SIZE 30
#define ESPNOW_NAME_LEN 10
#define ESPNOWCOM_MAX_DATA_LEN 255
#define ESPNOWCOM_MAX_SLAVES 1

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


// Data sructures to send / receive code

// Base Data strucutre

// Esp Now Datatypes
typedef enum{
    espnowCom_DataType_Mgmt
}espnowCom_DataType;

// Base Datastruct
// ----------------------------------------------------------------
// this struct is used to parse the data
// if the data is not a uint8_t array the data is obviously garbage
// however this struct is only needed to check the Type
// later the struct is casted to the apropriate struct based 
// on the type variable
typedef struct{
    uint8_t type;
    uint8_t data[];
}espnowCom_DataStruct_Base;

// standard Datatypes
// -----------------------------------------------------------------

// Datastruct for float
typedef struct{
    uint8_t type;
    float nbr;
}espnowCom_DataStruct_Float;

#define MAX_STRLEN
// Datastructure for string
typedef struct{
    uint8_t type;
    char string[10];
}espnowCom_DataStruct_String;

// espnowCom datastruct
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