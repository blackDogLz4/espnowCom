#include "espnowCom_Slave.h"

#define TAG "espnowCom_Slave"
#define DEBUG 1

#define SLAVENAME "FRANZI"

// private variables
static QueueHandle_t sendQueue;
static QueueHandle_t recvQueue;

static uint8_t broadcast_Mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t master_Mac[ESP_NOW_ETH_ALEN] = { 0 };


// private functions
static void _espnowCom_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void _espnowCom_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
static void _espnowCom_com_handler(void *payload);

int  _espnowCom_addMaster(uint8_t *mac, char name[]);
void _espnowCom_ACKMaster(uint8_t *mac, void *payload);
void _espnowCom_pingMaster(void *payload);

// user receiv_cb function array
void (*user_receive_cb_fn[30]) (int, void*, int) = { NULL };

/**
 *  @brief espnowCom_init
 * 
 *  function to intialize everything needed to use the espnowCom_Master
 */

int espnowCom_init(){
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
    
    // create send / recv callbacks
    ESP_ERROR_CHECK( esp_now_register_send_cb(_espnowCom_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(_espnowCom_recv_cb) );

    // create communication handler to hanle send and receive logic
    xTaskCreate(_espnowCom_com_handler, "espnowCom_com_handler", 2048, NULL, 4, NULL);    
    return ESP_OK;
}

/**
 * @brief espnowCom_send
 * 
 * function to send any data to master
 * 
 * @param type   to indicate what the data is
 * @param data   data to send 
 * @param size   size of data to send
 * 
 * @return 0     on success
 *        -1     error
 */
int espnowCom_send(int type, void *data, int size){
    espnowCom_sendEvent sendevt;
    espnowCom_DataStruct_Base *datastruct;
    
    // allocate memory
    datastruct = malloc(size + 1);
    if(datastruct == NULL){
        ESP_LOGE(TAG, "couldn't allocate memory for datastruct");
        return -1;
    }

    //prepeare data
    datastruct->type = espnowCom_DataType_Mgmt + type + 1;
    memcpy(datastruct->data, data, size);

    sendevt.len = size + 1;
    memcpy(sendevt.mac, master_Mac, ESP_NOW_ETH_ALEN);
    sendevt.send_data = (void*) datastruct;
    
    // put on Queue to send
    if(xQueueSend(sendQueue, &sendevt, 10 / portTICK_PERIOD_MS) != pdTRUE){
        if(DEBUG)
            ESP_LOGE(TAG, "send queue full!");
        return -1;
    }
    return 0;
}
/**
 * @brief espnowCom_addRecv_cb()
 * 
 * function to add a callback function when data of @type is received
 * 
 * @param type          type of data the function should be used for (0 to 29)
 * @param cb_function   function that is called after data is received
 * bsp: void myfn(int type, void* data, int size)
 * @return              0 on success
 *                      1 type already connected to a function
 *                     -1 on failure
 */

int espnowCom_addRecv_cb(int type, void (*cb_function) (int, void*, int)){
    //type not OK!!
    if(type < 0 || type >= 30){
        ESP_LOGE(TAG, "type not in Range!!");
        return -1;
    }
    if(user_receive_cb_fn[type] != NULL){
        ESP_LOGW(TAG, "type already connected to function");
        return 1;
    }
    user_receive_cb_fn[type] = cb_function;
    return 0;
}

/**
 * @brief _espnowCom_send_cb
 * 
 *  function gets called after data is sent with espnow
 * 
 */
static void _espnowCom_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status){
    if(mac_addr == NULL){
        ESP_LOGE(TAG, "sending failed: mac_addr is NULL...");
    }
    if(status == ESP_NOW_SEND_FAIL){
        ESP_LOGE(TAG, "sending failed: status failed");
    }
}

/**
 * @brief _espnowCom_recv_cb
 * 
 * function gets called when data is received
 * --> data is packed in espnowCom_recvEvent and put on the recv queue
 * 
 */
static void _espnowCom_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    uint8_t * mac_addr;
    espnowCom_recvEvent evt;
    
    if (recv_info->src_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    // allocate Buffer
    evt.recv_data = calloc(sizeof(uint8_t), len);
    if(evt.recv_data == NULL){
        ESP_LOGE(TAG, "couldn't allocate memory for evt.rcv_data");
        return;
    }

    //save src mac and type (broadcast / unicast)
    mac_addr = recv_info->src_addr;
    memcpy(evt.mac, mac_addr, ESP_NOW_ETH_ALEN);

    if(memcmp(broadcast_Mac, recv_info->des_addr, ESP_NOW_ETH_ALEN) == 0){
        evt.broadcast = true;
    }
    else{
        evt.broadcast = false;
    }

    // just copy the data
    memcpy((void*)evt.recv_data, data, len);

    evt.len = len;

    if (xQueueSend(recvQueue, &evt, 1 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGW(TAG, "sending to receive queue fail...");
    }
}

/**
 * @brief _espnowCom_com_handler
 * 
 * here the magic happens...
 * 
 */
static void _espnowCom_com_handler(void *payload){
    
    espnowCom_DataStruct_Base *recvstruct;
    espnowCom_DataStruct_Base user_recvstruct;
    espnowCom_DataStruct_Mgmt *mgmt_recvstruct;
    espnowCom_DataStruct_Mgmt  mgmt_sendstruct;
    espnowCom_recvEvent        recvevt;
    espnowCom_sendEvent        sendevt;

    uint8_t type;
    uint8_t ret;
    bool    broadcast = true;

    while(1){        
        // receive
        if(xQueueReceive(recvQueue, &recvevt, 10 / portTICK_PERIOD_MS) == pdTRUE){

            recvstruct = (espnowCom_DataStruct_Base *) recvevt.recv_data;

            switch(recvstruct->type){
                // Protokoll managment
                case espnowCom_DataType_Mgmt:
                    mgmt_recvstruct = (espnowCom_DataStruct_Mgmt *) recvevt.recv_data;
    
                    if(mgmt_recvstruct->mgmt_type == espnowCom_MGMT_TYPE_SYN){
        
                        // Add to peer list and send response
                        ret = _espnowCom_addMaster(recvevt.mac, mgmt_recvstruct->name);
                        vTaskDelay(10 / portTICK_PERIOD_MS);
                        if(ret > 0){
                            _espnowCom_ACKMaster(recvevt.mac, mgmt_recvstruct->payload);
                            broadcast = 0;
                        }
                    }
                    else if(mgmt_recvstruct->mgmt_type == espnowCom_MGMT_TYPE_PING){
                        // check just ping back
                        if(!broadcast){
                            if(DEBUG)
                                ESP_LOGI(TAG, "got pinged by master");
                            memcpy(mgmt_recvstruct->name, SLAVENAME, ESPNOW_NAME_LEN);
                            ret = esp_now_send(master_Mac, (uint8_t *) mgmt_recvstruct, sizeof(espnowCom_DataStruct_Mgmt));
                            if(ret != ESP_OK){
                                ESP_LOGW("TAG", "Couldn't reply ping!");
                            }
                        }
                        // master sending ping --> propably went offline
                        else{
                            // Add to peer list
                            ret = _espnowCom_addMaster(recvevt.mac, mgmt_recvstruct->name);
                            vTaskDelay(10 / portTICK_PERIOD_MS);
                            if(ret == 0){
                                broadcast = 0;
                            }
                        }
                    }
                    //free(recvevt.recv_data);
                    break;
                // receive user data    
                default:
                    if(DEBUG)
                        ESP_LOGI(TAG, "Received user Data");

                    recvstruct = (espnowCom_DataStruct_Base *) recvevt.recv_data;
                    type = recvstruct->type - espnowCom_DataType_Mgmt - 1;
                    
                    if(type < 30){
                        //function pointer already setup
                        
                        if(user_receive_cb_fn[type] != NULL){
                            user_receive_cb_fn[type](type, (void*) recvstruct->data, recvevt.len);
                        }
                        else{
                            if(DEBUG)
                                ESP_LOGI(TAG, "no handler for received Data!");
                        }
                    }
            }
        }    
        // send userData
        if(xQueueReceive(sendQueue, &sendevt, 10 / portTICK_PERIOD_MS) == pdTRUE){
            esp_now_send(sendevt.mac, sendevt.send_data, sendevt.len);
        }
    }
}

/**
 * @brief _espnowCom_addSlave
 * 
 *  well adds a Slave to the esp_Slave array
 * 
 *  return 0 on success
 *  return 1 master already added
 *  return -1 internall error
 */
int _espnowCom_addMaster(uint8_t *mac, char name[]){
    uint8_t ZERO[ESP_NOW_ETH_ALEN] = { 0 };
    esp_now_peer_info_t *peer = calloc(sizeof(esp_now_peer_info_t), 1);

    if(peer == NULL){
        ESP_LOGW(TAG, "couldn't allocate memory for peer info");
        return -1;
    }
    if(memcmp(master_Mac, ZERO, ESP_NOW_ETH_ALEN) == 0){
        // add to peer list - needed for espnow
        peer->channel = 0;
        peer->ifidx = ESP_IF_WIFI_STA;
        peer->encrypt = false;
        memcpy(peer->peer_addr, mac, ESP_NOW_ETH_ALEN);
        if(esp_now_add_peer(peer) != ESP_OK){
            ESP_LOGW(TAG, "couldn't add peer");
            free(peer);
            return -1;
        }
        free(peer);

        // copy master_Mac
        ESP_LOGI(TAG, "added %s", name);
        memcpy(master_Mac, mac, ESP_NOW_ETH_ALEN);

        return 0;
    }
    else{
        ESP_LOGI(TAG, "already connected to master");
        return 1;
    }
}

/**
 * @brief _espnowCom_ACKSlave()
 * 
 * function to send ACK to Slave and allow connection
 *
 */

void _espnowCom_ACKMaster(uint8_t *mac, void *payload){
    esp_err_t ret=4;
    espnowCom_DataStruct_Mgmt ack;
    
    ack.type = espnowCom_DataType_Mgmt;
    ack.mgmt_type = espnowCom_MGMT_TYPE_SYN_ACK;
    memcpy(ack.payload, payload, ESP_NOW_ETH_ALEN);
    memcpy(ack.name, SLAVENAME, ESPNOW_NAME_LEN);
    while(ret != ESP_OK){
        ret = esp_now_send(mac, (uint8_t *) &ack, sizeof(espnowCom_DataStruct_Mgmt));
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "couldn't send ACK! (%d)", ret);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}