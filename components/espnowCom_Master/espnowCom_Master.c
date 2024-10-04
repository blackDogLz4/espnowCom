#include "espnowCom_Master.h"

#define TAG "espnowCom_Master"

#define DEBUG 1
//#define DEBUG 0

// private variables
static QueueHandle_t sendQueue;
static QueueHandle_t recvQueue;

static uint8_t broadcast_Mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t master_Mac[ESP_NOW_ETH_ALEN] = {};

static int esp_SlavesNum = 0;

//private structs
struct Slave{
    bool connected;
    bool pingResp;
    uint8_t mac[ESP_NOW_ETH_ALEN];
    char name[ESPNOW_NAME_LEN];
};
struct Slave esp_Slaves[MAX_SLAVES];

// private functions
static void _espnowCom_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void _espnowCom_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
static void _espnowCom_com_handler(void *payload);

int  _espnowCom_addSlave(uint8_t *mac, char name[]);
void _espnowCom_ACKSlave(uint8_t *mac, void *payload);
void _espnowCom_pingSlaves(void *payload);
int  _espnowCom_MacToSlave(uint8_t *mac);

// user receiv_cb function array
void (*user_receive_cb_fn[30]) (int, int, void*) = { NULL };

/**
 *  @brief espnowCom_init
 * 
 *  function to intialize everything needed to use the espnowCom_Master
 */

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
    sendQueue = xQueueCreate(5, sizeof(espnowCom_sendEvent));
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
    
    //create payload
    payload = malloc(sizeof(uint8_t) * ESPNOWCOM_PAYLOADLEN);
    if(payload == NULL){
        ESP_LOGE(TAG, "couldn't reserve space for payload!!");
        ESP_ERROR_CHECK(false);
    }
    esp_fill_random(payload, sizeof(uint8_t)*ESPNOWCOM_PAYLOADLEN);

    // create send / recv callbacks
    ESP_ERROR_CHECK( esp_now_register_send_cb(_espnowCom_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(_espnowCom_recv_cb) );

    // create communication handler to hanle send and receive logic
    xTaskCreate(_espnowCom_com_handler, "espnowCom_com_handler", 2048, payload, 4, NULL);    
    return ESP_OK;
}
/**
 * @brief espnowCom_send
 * 
 * function to send any data to slave
 * 
 * @param slave  nbr of the slave to send data
 *               -1 broadcast to all slaves (not implemented yet!!)
 * @param type   to indicate what the data is
 * @param data   data to send 
 * @param size   size of data to send
 * 
 * @return 0     on success
 *         1     slave is offline
 *         2     slave doesn't exist
 *        -1     error
 */
int espnowCom_send(int slave, int type, void *data, int size){
    espnowCom_sendEvent sendevt;
    espnowCom_DataStruct_Base *datastruct;
    
    if(slave >= esp_SlavesNum){
        if(DEBUG)
            ESP_LOGW(TAG, "Slave doesn't exist");
        return 2;
    }
    if(esp_Slaves[slave].connected != true){
        if(DEBUG)
            ESP_LOGW(TAG, "Slave currently offline");
        return 1;
    }

    // allocate memory
    datastruct = malloc(size+1);
    if(datastruct == NULL){
        ESP_LOGE(TAG, "couldn't allocate memory for datastruct");
        return -1;
    }

    //prepeare data
    datastruct->type = espnowCom_DataType_Mgmt + type + 1;
    memcpy(datastruct->data, data, size);

    sendevt.len = size + 1;
    memcpy(sendevt.mac, esp_Slaves[slave].mac, ESP_NOW_ETH_ALEN);
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
 * bps: void myfn(int type, int slave, void* data)
 * @return              0 on success
 *                      1 type already connected to a function
 *                     -1 on failure
 */

int espnowCom_addRecv_cb(int type, void *cb_function (int, int, void*)){
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
        //propably offline
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

    //just copy the data
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
    espnowCom_DataStruct_Mgmt *mgmt_recvstruct;
    espnowCom_DataStruct_Mgmt  mgmt_sendstruct;
    espnowCom_recvEvent        recvevt;
    espnowCom_sendEvent        sendevt;
    TickType_t                 Ticks_lasteExec = 0;

    uint8_t ret;
    uint8_t type;
    bool    broadcast = true;

    while(1){        
        // receive
        if(xQueueReceive(recvQueue, &recvevt, 10 / portTICK_PERIOD_MS) == pdTRUE){

            recvstruct = (espnowCom_DataStruct_Base *) recvevt.recv_data;

            switch(recvstruct->type){
                // Protokoll managment
                case espnowCom_DataType_Mgmt:
                    mgmt_recvstruct = (espnowCom_DataStruct_Mgmt *) recvevt.recv_data;
    
                    if(mgmt_recvstruct->mgmt_type == espnowCom_MGMT_TYPE_SYN_ACK){
                        // --> new slave
                        // check for correct payload
                        if(memcmp(mgmt_recvstruct->payload, payload, ESPNOWCOM_PAYLOADLEN) != 0){
                            ESP_LOGI(TAG, "received wrong payload");
                            free(mgmt_recvstruct);
                        }
                        else{
                            // Add to peer list and send response
                            ret = _espnowCom_addSlave(recvevt.mac, mgmt_recvstruct->name);
                            if(ret == 0){
                                _espnowCom_ACKSlave(recvevt.mac, payload);
                            }
                            if(esp_SlavesNum >= MAX_SLAVES){
                                broadcast = false;
                            }
                        }
                    }
                    else if(mgmt_recvstruct->mgmt_type == espnowCom_MGMT_TYPE_PING){
                        // check if mac and mgmt_count are ok
                        if(mgmt_recvstruct->mgmt_count >= esp_SlavesNum){
                            ESP_LOGI(TAG, "mgmt_count too high!!");
                        }
                        else if(memcmp(esp_Slaves[mgmt_recvstruct->mgmt_count].mac, recvevt.mac, ESP_NOW_ETH_ALEN) != 0){
                            ESP_LOGI(TAG, "MAC doesn't match!");
                        }
                        else if(memcmp(mgmt_recvstruct->payload, payload, ESPNOWCOM_PAYLOADLEN) != 0){
                            ESP_LOGI(TAG, "Payload doesn't match!");
                        }
                        else{
                            if(DEBUG)
                                ESP_LOGI(TAG, "keepalive signale from %s", esp_Slaves[mgmt_recvstruct->mgmt_count].name);
                            
                            esp_Slaves[mgmt_recvstruct->mgmt_count].pingResp = true;
                        }
                    }
                    free(recvevt.recv_data);
                    break;
                // receive user data    
                default:
                    if(DEBUG)
                        ESP_LOGI(TAG, "Received user Data");

                    recvstruct = (espnowCom_DataStruct_Base *) recvevt.recv_data;
                    type = recvstruct->type;
                    
                    if(type < 30){
                        //function pointer already setup
                        if(user_receive_cb_fn[type] != NULL){
                            user_receive_cb_fn[type](type, _espnowCom_MacToSlave(recvevt.mac), (void*) recvstruct->data);
                        }
                        else{
                            if(DEBUG)
                                ESP_LOGI(TAG, "no handler for received Data!");
                        }
                    }
            }
        }
        // send mngmt every 1000ms
        if(xTaskGetTickCount() - Ticks_lasteExec > 1000 / portTICK_PERIOD_MS){
            
            if(broadcast){
                if(DEBUG)
                    ESP_LOGI(TAG, "sending boradcast");
                mgmt_sendstruct.type = espnowCom_DataType_Mgmt;
                mgmt_sendstruct.mgmt_type = espnowCom_MGMT_TYPE_SYN;
                memcpy(mgmt_sendstruct.payload, payload, ESPNOWCOM_PAYLOADLEN);
                memcpy(mgmt_sendstruct.name, "master", 10);

                esp_now_send(broadcast_Mac, (uint8_t* ) &mgmt_sendstruct, sizeof(espnowCom_DataStruct_Mgmt));

                Ticks_lasteExec = xTaskGetTickCount();
            }
            else{
                if(DEBUG)
                    ESP_LOGI(TAG, "sending ping");
                _espnowCom_pingSlaves(payload);
                Ticks_lasteExec = xTaskGetTickCount();
            }
        }   
        
        // send userData
        if(xQueueReceive(sendQueue, &sendevt, 10 / portTICK_PERIOD_MS) == pdTRUE){
            recvstruct = (espnowCom_DataStruct_Base *) sendevt.send_data;
            ESP_LOGI(TAG, "sending %d, %s, %d", recvstruct->type, (char*) recvstruct->data, sendevt.len);
            esp_now_send(sendevt.mac, (uint8_t*) sendevt.send_data, sendevt.len);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            //free(sendevt.send_data);
        }
    }
}

/**
 * @brief _espnowCom_addSlave
 * 
 *  well adds a Slave to the esp_Slave array
 * 
 *  return 0 on success
 *  return 1 array full
 *  return -1 internall error
 */
int _espnowCom_addSlave(uint8_t *mac, char name[]){
    esp_now_peer_info_t *peer = calloc(sizeof(esp_now_peer_info_t), 1);

    if(peer == NULL){
        ESP_LOGW(TAG, "couldn't allocate memory for peer info");
        return -1;
    }
    if(esp_SlavesNum < MAX_SLAVES){

        // slave already connected?
        if(esp_now_is_peer_exist(mac)){
            ESP_LOGI(TAG, "Slave already connected");
            return 0;
        }

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

        // add to slave list
        ESP_LOGI(TAG, "added %s", name);

        esp_Slaves[esp_SlavesNum].connected = true;
        esp_Slaves[esp_SlavesNum].pingResp = true;
        memcpy(esp_Slaves[esp_SlavesNum].mac, mac, ESP_NOW_ETH_ALEN);
        memcpy(esp_Slaves[esp_SlavesNum].name, name, ESPNOW_NAME_LEN);
        esp_SlavesNum++;
        return 0;
    }
    else{
        ESP_LOGI(TAG, "max. Slaves connected!");
        return 1;
    }

}

/**
 * @brief _espnowCom_ACKSlave()
 * 
 * function to send ACK to Slave and allow connection
 *
 */

void _espnowCom_ACKSlave(uint8_t *mac, void *payload){
    esp_err_t ret;
    espnowCom_DataStruct_Mgmt ack;
    
    ack.type = espnowCom_DataType_Mgmt;
    ack.mgmt_type = espnowCom_MGMT_TYPE_ACK;
    memcpy(ack.payload, payload, ESP_NOW_ETH_ALEN);

    ret = esp_now_send(mac, (uint8_t *) &ack, sizeof(espnowCom_DataStruct_Mgmt));
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "couldn't send ACK!");
    }
}
/**
 * @brief _espnowCom_pingSlaves
 * 
 * function sends Keepalive Signal to all slaves
 * 
 */
void _espnowCom_pingSlaves(void *payload){
    esp_err_t ret;
    static espnowCom_DataStruct_Mgmt ping;
    
    ping.type = espnowCom_DataType_Mgmt;
    ping.mgmt_type = espnowCom_MGMT_TYPE_PING;
    memcpy(ping.name, "master", ESPNOW_NAME_LEN);
    memcpy(ping.payload, payload, ESPNOWCOM_PAYLOADLEN);

    for(int i = 0; i < esp_SlavesNum; i++){
        // check if we received ping response
        if(esp_Slaves[i].pingResp != true){
            if(DEBUG)
                ESP_LOGW(TAG, "%s disconnected", esp_Slaves[i].name);
            esp_Slaves[i].connected = false;
        }
        else{
            esp_Slaves[i].connected = true;
        }
        ping.mgmt_count = i;
        ret = esp_now_send(esp_Slaves[i].mac, (uint8_t *) &ping, sizeof(espnowCom_DataStruct_Mgmt));
        
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "couldn't send ping!");
        }
        else{
            esp_Slaves[i].pingResp = false;
        }
    }
}
/**
 * @brief espnowCom_MacToSlave
 * 
 * function to find the slave number connected to the mac
 * 
 * @param mac
 * @return slavenbr on success
 *         -1 on failure
 */
int  _espnowCom_MacToSlave(uint8_t *mac){

    for(int i = 0; i<esp_SlavesNum; i++){
        if(memcmp(esp_Slaves[i].mac, mac, ESP_NOW_ETH_ALEN) == 0){
            return i;
        }
    }
    return -1;
}