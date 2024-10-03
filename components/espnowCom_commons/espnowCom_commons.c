/* myespnow.c */
#include "espnowCom_commons.h"

#ifdef CONFIG_ESPNOWCOM_MASTERMODE
    #define TAG "espnowCom_Master"
#else
    #define TAG "espnowCom_Slave"
#endif

// private variables
static QueueHandle_t sendQueue;
static QueueHandle_t recvQueue;
static QueueHandle_t recvQueueData;

static uint8_t broadcast_Mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t master_Mac[ESP_NOW_ETH_ALEN] = {};
static uint8_t **slaves_Mac;

// send / receive Buffer
static uint8_t *sendDataBuffer;

static SemaphoreHandle_t sendmutex;

// mutex for state switching in Send/Recevie Tasks
static SemaphoreHandle_t state_mutex;
static int state_current = espnowCom_Sate_Connect;

// private functions
static void espnowCom_send_stringTask(void *payload);
static void espnowCom_RecvTask(void *payload);
static void _espnowCom_send_string_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void _espnowCom_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
int espnowCom_addSlave(uint8_t *mac);
void espnowCom_addMaster(uint8_t *mac);

// parse DataStructs
static void processData(char *str, espnowCom_DataStruct_Base *data_base);

// connection functions
void espnowCom_sendSlaveACK(uint8_t *mac, uint8_t *payload);
void espnowCom_sendMasterACK(uint8_t *mac);

int espnowCom_init(){
    uint8_t *payload;

    // initialize Wifi in AP_Mode
    ESP_LOGI(TAG, "Initialize Wifi");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start());

    // enable long range
    //ESP_ERROR_CHECK( esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
    
    // initialize esp now
    ESP_ERROR_CHECK( esp_now_init() );

    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    // initialize basic send functionality
    sendQueue = xQueueCreate(1, sizeof(espnowCom_sendEvent));
    if (sendQueue == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }

    // initialize basic recv functionality
    recvQueue = xQueueCreate(5, sizeof(espnowCom_recvEvent));
    if (recvQueue == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }

    // queue for received Data
    recvQueueData = xQueueCreate(10, sizeof(espnowCom_recvEvent));
    if (recvQueueData == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }


    // create mutex for state switching
    state_mutex = xSemaphoreCreateMutex();

    // create mutex for send
    sendmutex = xSemaphoreCreateMutex();


    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 0;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcast_Mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    // reserve memory for send buffer
    sendDataBuffer = calloc(ESPNOWCOM_MAX_DATA_LEN, sizeof(uint8_t));
    if(sendDataBuffer == NULL){
        ESP_LOGE(TAG, "couldn't reserve space for send Data!!");
        ESP_ERROR_CHECK(false);
    }

    ESP_ERROR_CHECK( esp_now_register_send_cb(_espnowCom_send_string_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(_espnowCom_recv_cb) );
    
    // allocate memory for slaves mac
    if(MASTER){
        slaves_Mac = malloc(sizeof(uint8_t *) * ESPNOWCOM_MAX_SLAVES);
        for(int i = 0; i<ESPNOWCOM_MAX_SLAVES; i++){
            slaves_Mac[i] = malloc(sizeof(uint8_t) *  ESP_NOW_ETH_ALEN);
        }
    }

    if(MASTER){
        //create payload
        payload = malloc(sizeof(uint8_t) * ESPNOWCOM_PAYLOADLEN);
        if(payload == NULL){
            ESP_LOGE(TAG, "couldn't reserve space for payload!!");
            ESP_ERROR_CHECK(false);
        }
        esp_fill_random(payload, sizeof(uint8_t)*ESPNOWCOM_PAYLOADLEN);
        xTaskCreate(espnowCom_send_stringTask, "espnowCom_send_stringTask", 2048, payload, 4, NULL);
        xTaskCreate(espnowCom_RecvTask, "espnowCom_recvTask", 2048, payload, 4, NULL);
    }
    else{
        xTaskCreate(espnowCom_send_stringTask, "espnowCom_send_stringTask", 2048, NULL, 4, NULL);
        xTaskCreate(espnowCom_RecvTask, "espnowCom_recvTask", 2048, NULL, 4, NULL);
    }

    return ESP_OK;
}

void espnowCom_deinit(){
    esp_now_deinit();
}
static void _espnowCom_send_string_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }
}
static void _espnowCom_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    //ESP_LOGI(TAG, "Received something");
    // get mac info
    uint8_t * mac_addr = recv_info->src_addr;
    //uint8_t * des_addr = recv_info->des_addr;

    espnowCom_recvEvent evt;
    espnowCom_DataStruct_Base *datastruct;
    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    //set Buffer
    evt.recv_data = malloc(sizeof(uint8_t) * len);
    datastruct = (espnowCom_DataStruct_Base *) evt.recv_data;
    memcpy(evt.mac, mac_addr, ESP_NOW_ETH_ALEN);

    datastruct->type = data[0];
    memcpy(datastruct->data, &data[1], len-1);
    
    evt.len = len;
    if (xQueueSend(recvQueue, &evt, 1 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
    }
}
static void espnowCom_send_stringTask(void *payload){
   
    espnowCom_sendEvent evt;
    volatile espnowCom_States current = espnowCom_Sate_Connect;
    
    espnowCom_DataStruct_Base *data;
    espnowCom_DataStruct_Mgmt mgmtData;

    int ret;
    int cnt = 0;
    // send
    if(MASTER){
        mgmtData.type = espnowCom_DataType_Mgmt;
        mgmtData.mgmt_type = espnowCom_MGMT_TYPE_DISCOVER;
        memcpy(mgmtData.payload, payload, sizeof(uint8_t)*ESPNOWCOM_PAYLOADLEN);
    }
    while(1){
        if(xSemaphoreTake(state_mutex, 1 / portTICK_PERIOD_MS)){
            current = state_current;
            vTaskDelay(10 / portTICK_PERIOD_MS);
            xSemaphoreGive(state_mutex);
        }

        if(MASTER == 1){
            switch(current){
                case espnowCom_Sate_Connect:
                    
                    mgmtData.mgmt_count = cnt;
                    cnt++;

                    memcpy(evt.mac, broadcast_Mac, ESP_NOW_ETH_ALEN);
                    evt.len = sizeof(mgmtData);
                    evt.send_data = (uint8_t *) &mgmtData;
                    // visualisation
                    data = (espnowCom_DataStruct_Base *) evt.send_data;
                    processData("sending: ", data);

                    ret = esp_now_send(evt.mac , (uint8_t*) evt.send_data, evt.len);
                    
                    if(ret != ESP_OK){
                        ESP_LOGE(TAG, "Sending failed, %d", ret);
                        //espnowCom_deinit();
                    }
                    
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    break;
                case espnowCom_Sate_Run:
                if (xQueueReceive(sendQueue, &evt, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                        
                        // just for visualisation
                        data = (espnowCom_DataStruct_Base *) evt.send_data;
                        processData("sending: ", data);

                        ret = esp_now_send(evt.mac , (uint8_t*) evt.send_data, evt.len);
                        if(ret != ESP_OK){
                            ESP_LOGE(TAG, "Sending failed, %d", ret);
                            //espnowCom_deinit();
                        }
                        //vTaskDelay(portTICK_PERIOD_MS);
                    }
                    break;
            }
        }

        else{
            switch(current){
                case espnowCom_Sate_Connect:
                    ESP_LOGI(TAG, "Wait ...");
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    break;
                case espnowCom_Sate_Run:
                if (xQueueReceive(sendQueue, &evt, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                        
                        // just for visualisation
                        data = (espnowCom_DataStruct_Base *) evt.send_data;
                        processData("sending: ", data);

                        ret = esp_now_send(evt.mac , (uint8_t*) evt.send_data, evt.len);
                        if(ret != ESP_OK){
                            ESP_LOGE(TAG, "Sending failed, %d", ret);
                        }
                    }
                    break;
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }   
}
static void espnowCom_RecvTask(void *payload){
   
    espnowCom_recvEvent recvevt;
    volatile espnowCom_States current = espnowCom_Sate_Connect;
    espnowCom_DataStruct_Base *data;
    espnowCom_DataStruct_Mgmt *mgmtData;
    uint8_t recv_payload[ESPNOWCOM_PAYLOADLEN];
    uint8_t *payloadarray = (uint8_t *) payload;
    int ret;
    // receive
    while(1){
        if(xSemaphoreTake(state_mutex, 1 / portTICK_PERIOD_MS)){
            current = state_current;
            vTaskDelay(10 / portTICK_PERIOD_MS);
            xSemaphoreGive(state_mutex);
        }
        if(MASTER){
            switch(current){
                case espnowCom_Sate_Connect:
                    
                    if(xQueueReceive(recvQueue, &recvevt, 100 / portTICK_PERIOD_MS) == pdTRUE){
                        // received connection ACK from Slave?
                        mgmtData = (espnowCom_DataStruct_Mgmt *) recvevt.recv_data;
                        if(mgmtData->type == espnowCom_DataType_Mgmt){
                            if(mgmtData->mgmt_type == espnowCom_MGMT_TYPE_CONNECT){
                                ESP_LOGI(TAG, "got connection response!");
                                if( memcmp(payloadarray, mgmtData->payload, ESPNOWCOM_PAYLOADLEN) == 0){
                                    ESP_LOGI(TAG, "Payload ok!");
                                
                                    // stop when all slaves are added
                                    if(espnowCom_addSlave(recvevt.mac)){
                                        espnowCom_sendMasterACK(recvevt.mac);
                                        espnowCom_switchMode(espnowCom_Sate_Run);
                                    }
                                    else{
                                        espnowCom_sendMasterACK(recvevt.mac);
                                    }
                                    free(recvevt.recv_data);
                                }
                                else{
                                    ESP_LOGE(TAG, "Wrong Payload");
                                    ESP_LOGI(TAG, "payload recv:  0x%x 0x%x 0x%x 0x%x", mgmtData->payload[0], mgmtData->payload[1], mgmtData->payload[2], mgmtData->payload[3]);
                                    ESP_LOGI(TAG, "payload expected:  0x%x 0x%x 0x%x 0x%x", payloadarray[0], payloadarray[1], payloadarray[2], payloadarray[3]);
                                }
                            }
                            
                        }
                    }
                    break;
                case espnowCom_Sate_Run:
                    if (xQueueReceive(recvQueue, &recvevt, 100 / portTICK_PERIOD_MS) == pdTRUE){
                        data = (espnowCom_DataStruct_Base *) recvevt.recv_data;
                        // just for visualisation
                        processData("received", data);
                        free(recvevt.recv_data);
                    }
                    break;
                break;
            vTaskDelay(1 / portTICK_PERIOD_MS);
            }
        }
        else{
            switch(current){
                case espnowCom_Sate_Connect:
                    if (xQueueReceive(recvQueue, &recvevt, 100 / portTICK_PERIOD_MS) == pdTRUE){
                        mgmtData = (espnowCom_DataStruct_Mgmt *) recvevt.recv_data;
                        if(mgmtData->type == espnowCom_DataType_Mgmt){
                            if(mgmtData->mgmt_type == espnowCom_MGMT_TYPE_DISCOVER){
                                ESP_LOGI(TAG, "Conection request %d", mgmtData->mgmt_count);

                                espnowCom_addMaster(recvevt.mac);

                                memcpy(recv_payload, mgmtData->payload, sizeof(uint8_t) * ESPNOWCOM_PAYLOADLEN);
                                espnowCom_sendSlaveACK(recvevt.mac, recv_payload);
                                free(recvevt.recv_data);
                            }
                            else if(mgmtData->mgmt_type == espnowCom_MGMT_TYPE_CONNECTED){
                                ESP_LOGI(TAG, "Conected!!");
                                espnowCom_switchMode(espnowCom_Sate_Run);
                            }
                        }
                    }
                    break;
                case espnowCom_Sate_Run:
                    if (xQueueReceive(recvQueue, &recvevt, 100 / portTICK_PERIOD_MS) == pdTRUE){
                        data = (espnowCom_DataStruct_Base *) recvevt.recv_data;
                        // just for visualisation
                        processData("received", data);

                        if(data->type == espnowCom_DataType_Mgmt){
                            mgmtData = (espnowCom_DataStruct_Mgmt *) data;
                            if(data->type == espnowCom_MGMT_TYPE_DISCOVER && memcmp(master_Mac, recvevt.mac, ESP_NOW_ETH_ALEN) == 0){
                                // master propably reseted or waiting for other devices
                                memcpy(recv_payload, mgmtData->payload, sizeof(uint8_t) * ESPNOWCOM_PAYLOADLEN);
                                espnowCom_sendSlaveACK(recvevt.mac, recv_payload);
                            } 
                            free(recvevt.recv_data);
                        }
                        else{
                            // put Data on Data queue to be received by user
                            xQueueSend(recvQueueData, &recvevt, 10 / portTICK_PERIOD_MS);
                            //note recvevt.recv_data needs to bee freed 
                        }
                    }
                    break;
                break;
            vTaskDelay(1 / portTICK_PERIOD_MS);
            }
        }
    }   
}
/* function to send ack from slave*/
void espnowCom_sendSlaveACK(uint8_t *mac, uint8_t *payload){
    espnowCom_DataStruct_Mgmt slaveResponse;
    int ret;
    slaveResponse.type = espnowCom_DataType_Mgmt;
    slaveResponse.mgmt_type = espnowCom_MGMT_TYPE_CONNECT;
    memcpy(slaveResponse.payload, payload, sizeof(uint8_t) * ESPNOWCOM_PAYLOADLEN);
    processData("sending", &slaveResponse);
    ret = esp_now_send(mac, (uint8_t*) &slaveResponse, sizeof(slaveResponse));
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Sending failed, %d", ret);
        ESP_LOGI(TAG, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        //espnowCom_deinit();
    }
}

/* add slave to peer list

    return 1 -- slavelist is full
    return 0 -- all ok
*/
int espnowCom_addSlave(uint8_t *mac){
    static int slave_cnt = 0;
    esp_now_peer_info_t *peer;
    
    if(slave_cnt >= ESPNOWCOM_MAX_SLAVES){
        return 1;
    }
    if(esp_now_is_peer_exist(mac) != 1){
        memcpy(slaves_Mac[slave_cnt], mac, sizeof(uint8_t) * ESP_NOW_ETH_ALEN);
        slave_cnt++;                                
        
        peer = malloc(sizeof(esp_now_peer_info_t));
        if (peer == NULL) {
            ESP_LOGE(TAG, "Malloc peer information fail");
        }
        memset(peer, 0, sizeof(esp_now_peer_info_t));
        peer->channel = 0;
        peer->ifidx = ESP_IF_WIFI_STA;
        peer->encrypt = false;
        memcpy(peer->peer_addr, mac, ESP_NOW_ETH_ALEN);
        esp_now_add_peer(peer);
        free(peer);
    }
    if(slave_cnt >= ESPNOWCOM_MAX_SLAVES){
        return 1;
    }
    return 0;
}
void espnowCom_addMaster(uint8_t *mac){
    
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    /* Add new peer information to peer list. */
    if(esp_now_is_peer_exist(mac) != 1){
        memcpy(master_Mac, mac, ESP_NOW_ETH_ALEN);

        if (peer == NULL) {
            ESP_LOGE(TAG, "Malloc peer information fail");
        }
        memset(peer, 0, sizeof(esp_now_peer_info_t));
        peer->channel = 0;
        peer->ifidx = ESP_IF_WIFI_STA;
        peer->encrypt = false;
        memcpy(peer->peer_addr, mac, ESP_NOW_ETH_ALEN);
        esp_now_add_peer(peer);
        free(peer);                                
    }
}
/* function to send ack from master*/
void espnowCom_sendMasterACK(uint8_t *mac){
    espnowCom_DataStruct_Mgmt slaveResponse;
    int ret;
    slaveResponse.type = espnowCom_DataType_Mgmt;
    slaveResponse.mgmt_type = espnowCom_MGMT_TYPE_CONNECTED;
    processData("sending", &slaveResponse);
    ret = esp_now_send(mac, (uint8_t*) &slaveResponse, sizeof(sizeof(slaveResponse)));
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Sending failed, %d", ret);
        ESP_LOGI(TAG, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        //espnowCom_deinit();
    }
}

void espnowCom_switchMode(espnowCom_States state){
    
    if(xSemaphoreTake(state_mutex, portMAX_DELAY)){
        state_current = state;
        vTaskDelay(10 / portTICK_PERIOD_MS);
        xSemaphoreGive(state_mutex);
    }
    else{
        ESP_LOGW(TAG, "Can't switch mode");
    }
}

void espnowCom_slave_send_string(char *str){
    static espnowCom_sendEvent evt = { 0 };
    static espnowCom_DataStruct_String *datastruct;
    // broadcast string
    memcpy((void *)evt.mac, (void *)broadcast_Mac, ESP_NOW_ETH_ALEN);

    evt.len = strlen(str) + 2;
    
    if(evt.len > ESPNOWCOM_MAX_DATA_LEN){
        ESP_LOGE(TAG, "Too much Data to send!!");
        return;
    }

    //write Data to Buffer
    //  get mutex
    if(xSemaphoreTake(sendmutex, portMAX_DELAY)){
        evt.send_data = (void *) sendDataBuffer;
        datastruct = (espnowCom_DataStruct_String *) sendDataBuffer;
        datastruct->type = espnowCom_DataType_String;
        memcpy((void *)datastruct->string, (void *) str, evt.len); 

        if (xQueueSend(sendQueue, &evt, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to send data to queue.");
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
        // data on queue soo all ayt
        xSemaphoreGive(sendmutex);
    }
    else{
        ESP_LOGE(TAG, "couldn't unlock mutex :(");
    }
    
}

void espnowCom_master_send_string(int slave_nbr, char *str){
    static espnowCom_sendEvent evt = { 0 };
    static espnowCom_DataStruct_String *datastruct;

    if(slave_nbr >= ESPNOWCOM_MAX_SLAVES){
        return;
    }
    memcpy((void *)evt.mac, (void *)slaves_Mac[slave_nbr], ESP_NOW_ETH_ALEN);

    evt.len = strlen(str) + 2;
    
    if(evt.len > ESPNOWCOM_MAX_DATA_LEN){
        ESP_LOGE(TAG, "Too much Data to send!!");
        return;
    }

    //write Data to Buffer
    //  get mutex
    if(xSemaphoreTake(sendmutex, portMAX_DELAY)){
        evt.send_data = (void *) sendDataBuffer;
        datastruct = (espnowCom_DataStruct_String *) sendDataBuffer;
        datastruct->type = espnowCom_DataType_String;
        memcpy((void *)datastruct->string, (void *) str, evt.len); 

        if (xQueueSend(sendQueue, &evt, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to send data to queue.");
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
        // data on queue soo all ayt
        xSemaphoreGive(sendmutex);
    }
    else{
        ESP_LOGE(TAG, "couldn't unlock mutex :(");
    }
    
}

void espnowCom_master_send_float(int slave_nbr, float fl){
    static espnowCom_sendEvent evt = { 0 };
    static espnowCom_DataStruct_Float *datastruct;
    
    if(slave_nbr >= ESPNOWCOM_MAX_SLAVES){
        return;
    }
    memcpy((void *)evt.mac, (void *)slaves_Mac[slave_nbr], ESP_NOW_ETH_ALEN);

    evt.len = sizeof(espnowCom_DataStruct_Float);
    
    if(evt.len > ESPNOWCOM_MAX_DATA_LEN){
        ESP_LOGE(TAG, "Too much Data to send!!");
        return;
    }

    //write Data to Buffer
    //  get mutex
    if(xSemaphoreTake(sendmutex, portMAX_DELAY)){
        evt.send_data = (void *) sendDataBuffer;
        datastruct = (espnowCom_DataStruct_Float *) sendDataBuffer;

        datastruct->type = espnowCom_DataType_Float;
        datastruct->nbr = fl;

        if (xQueueSend(sendQueue, &evt, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to send data to queue.");
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // data on queue soo all ayt
        xSemaphoreGive(sendmutex);
    }
    else{
        ESP_LOGE(TAG, "couldn't unlock mutex :(");
    }
}

void espnowCom_slave_send_float(float fl){
    static espnowCom_sendEvent evt = { 0 };
    static espnowCom_DataStruct_Float *datastruct;
    // broadcast string
    memcpy((void *)evt.mac, (void *)broadcast_Mac, ESP_NOW_ETH_ALEN);

    evt.len = sizeof(espnowCom_DataStruct_Float);
    
    if(evt.len > ESPNOWCOM_MAX_DATA_LEN){
        ESP_LOGE(TAG, "Too much Data to send!!");
        return;
    }

    //write Data to Buffer
    //  get mutex
    if(xSemaphoreTake(sendmutex, portMAX_DELAY)){
        evt.send_data = (void *) sendDataBuffer;
        datastruct = (espnowCom_DataStruct_Float *) sendDataBuffer;

        datastruct->type = espnowCom_DataType_Float;
        datastruct->nbr = fl;

        if (xQueueSend(sendQueue, &evt, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to send data to queue.");
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // data on queue soo all ayt
        xSemaphoreGive(sendmutex);
    }
    else{
        ESP_LOGE(TAG, "couldn't unlock mutex :(");
    }
}

/**
 * @brief processData
 * 
 * checks the datastruct and prints output accoring to its type
 * 
 */

static void processData(char *str, espnowCom_DataStruct_Base *data_base){
    
    espnowCom_DataStruct_Float *data_float = (espnowCom_DataStruct_Float *) data_base;
    espnowCom_DataStruct_String *data_string = (espnowCom_DataStruct_String *) data_base;
    espnowCom_DataStruct_Mgmt *data_mgmt = (espnowCom_DataStruct_Mgmt *) data_base;

    switch(data_base->type){
        case espnowCom_DataType_Float:
            ESP_LOGI(TAG, "%s: %f", str , data_float->nbr);
            break;
        case espnowCom_DataType_String:
            ESP_LOGI(TAG, "%s: %s", str, data_string->string);
            break;
        case espnowCom_DataType_Mgmt:
            switch(data_mgmt->mgmt_type){
                case espnowCom_MGMT_TYPE_DISCOVER:
                    ESP_LOGI(TAG, "%s DISCOVER req, cnt: %d", str, data_mgmt->mgmt_count);
                    ESP_LOGI(TAG, "payload:  0x%x 0x%x 0x%x 0x%x", data_mgmt->payload[0], data_mgmt->payload[1], data_mgmt->payload[2], data_mgmt->payload[3]); 
                    break;
                case espnowCom_MGMT_TYPE_CONNECT:
                    ESP_LOGI(TAG, "%s CONNECT req", str);
                    ESP_LOGI(TAG, "payload:  0x%x 0x%x 0x%x 0x%x", data_mgmt->payload[0], data_mgmt->payload[1], data_mgmt->payload[2], data_mgmt->payload[3]); 
                    break;
            }
    }
}