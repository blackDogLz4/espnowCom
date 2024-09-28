#ifndef MYESPNOW_DATASTRUCT_H
#define MYESPNOW_DATASTRUCT_H

#include <stdint.h>

// Data sructures to send / receive code

// Base Data strucutre

// Esp Now Datatypes
typedef enum{
    espnowCom_DataType_String,
    espnowCom_DataType_Float,
    espnowCom_DataType_Mgmt,
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
}espnowCom_DataStruct_Mgmt;

// enum mgmt_type
typedef enum{
    espnowCom_MGMT_TYPE_DISCOVER,   // sent by master to discover devices
    espnowCom_MGMT_TYPE_CONNECT,    // sent by slave to connect
    espnowCom_MGMT_TYPE_CONNECTED   // sent by master after connect
}espnowCom_MGMT_TYPE;   

#endif