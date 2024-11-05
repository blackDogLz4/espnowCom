#include "espnowCom.h"

#ifdef CONFIG_ESPNOWCOM_MASTERMODE
    #define TAG "espnowCom_Master"
#else
    #define TAG "espnowCom_Slave"
#endif

#ifndef CONFIG_ESPNOWCOM_DEBUG
    #define CONFIG_ESPNOWCOM_DEBUG 0
#else
    #define CONFIG_ESPNOWCOM_DEBUG 1
#endif
// private variables
static QueueHandle_t sendQueue;
static QueueHandle_t recvQueue;

// mutex for sending
SemaphoreHandle_t sendSemaphore;

// mutex to signal that handler function should be continued
SemaphoreHandle_t sendReceiveSemaphore;

static uint8_t broadcast_Mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// user receiv_cb function array
espnowCom_user_receive_cb user_receive_cb_fn[CONFIG_ESPNOWCOM_MAX_CB_FUNCTIONS] = { NULL };

// private functions
static void _espnowCom_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void _espnowCom_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
static void _espnowCom_com_handler(void *payload);


#ifdef CONFIG_ESPNOWCOM_MASTERMODE
static int esp_SlavesNum = 0;

//private structs
struct Slave{
    bool connected;
    bool pingResp;
    uint8_t mac[ESP_NOW_ETH_ALEN];
    char name[ESPNOW_NAME_LEN];
};
struct Slave esp_Slaves[CONFIG_ESPNOWCOM_MAX_SLAVES];

int  _espnowCom_addSlave(uint8_t *mac, char name[]);
void _espnowCom_ACKSlave(uint8_t *mac, void *payload);
void _espnowCom_pingSlaves(void *payload);
int  _espnowCom_MacToSlave(uint8_t *mac);
#else
static uint8_t master_Mac[ESP_NOW_ETH_ALEN] = {};
int  _espnowCom_addMaster(uint8_t *mac, char name[]);
void _espnowCom_ACKMaster(uint8_t *mac, void *payload);
void _espnowCom_pingMaster(void *payload);
#endif

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
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOWCOM_CHANNEL, WIFI_SECOND_CHAN_NONE) );
    
    // enable long range
    if(ESPNOWCOM_LONGRANGE){
        ESP_ERROR_CHECK( esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
    }
    // initialize esp now
    ESP_ERROR_CHECK( esp_now_init() );

    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOWCOM_PMK) );

    // initialize basic send functionality
    sendQueue = xQueueCreate(CONFIG_ESPNOWCOM_QUEUE_LEN, sizeof(espnowCom_sendEvent));
    if (sendQueue == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }

    // initialize basic recv functionality
    recvQueue = xQueueCreate(CONFIG_ESPNOWCOM_QUEUE_LEN, sizeof(espnowCom_recvEvent));
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

    // create send / recv callbacks
    ESP_ERROR_CHECK( esp_now_register_send_cb(_espnowCom_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(_espnowCom_recv_cb) );

    // create semaphore for send
    sendSemaphore = xSemaphoreCreateBinary();
    if(sendSemaphore == NULL){
        ESP_LOGE(TAG, "Malloc Semaphore fail");
        esp_now_deinit();
        return ESP_FAIL;
    }

    // create semaphore to signal when data is available to send / receive
    sendReceiveSemaphore = xSemaphoreCreateBinary();
    if(sendSemaphore == NULL){
        ESP_LOGE(TAG, "Malloc Semaphore fail");
        esp_now_deinit();
        return ESP_FAIL;
    }

#ifdef CONFIG_ESPNOWCOM_MASTERMODE
    //create payload
    uint8_t *payload = malloc(sizeof(uint8_t) * ESPNOWCOM_PAYLOADLEN);
    if(payload == NULL){
        ESP_LOGE(TAG, "couldn't reserve space for payload!!");
        ESP_ERROR_CHECK(false);
    }
    esp_fill_random(payload, sizeof(uint8_t)*ESPNOWCOM_PAYLOADLEN);

    // create communication handler to hanle send and receive logic
    xTaskCreatePinnedToCore(_espnowCom_com_handler, "espnowCom_com_handler", 2048, payload, 4, NULL, 1);    
#else
    xTaskCreatePinnedToCore(_espnowCom_com_handler, "espnowCom_com_handler", 2048, NULL, 4, NULL, 1);
#endif    
    return ESP_OK;
}
#ifdef CONFIG_ESPNOWCOM_MASTERMODE

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
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if(slave >= esp_SlavesNum){
        if(CONFIG_ESPNOWCOM_DEBUG)
            ESP_LOGW(TAG, "Slave doesn't exist");
        return 2;
    }
    if(esp_Slaves[slave].connected != true){
        if(CONFIG_ESPNOWCOM_DEBUG)
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
    sendevt.send_data = (void*) datastruct;

    memcpy(sendevt.mac, esp_Slaves[slave].mac, ESP_NOW_ETH_ALEN);
    // put on Queue to send
    if(xQueueSend(sendQueue, &sendevt, 10 / portTICK_PERIOD_MS) != pdTRUE){
        if(CONFIG_ESPNOWCOM_DEBUG)
            ESP_LOGE(TAG, "send queue full!");
        xSemaphoreGiveFromISR(sendReceiveSemaphore, &xHigherPriorityTaskWoken);
        return -1;
    }
    xSemaphoreGiveFromISR(sendReceiveSemaphore, &xHigherPriorityTaskWoken);
    return 0;
}
#else
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
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

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
        if(CONFIG_ESPNOWCOM_DEBUG)
            ESP_LOGE(TAG, "send queue full!");
        xSemaphoreGiveFromISR(sendReceiveSemaphore, &xHigherPriorityTaskWoken);
        return -1;
    }
    xSemaphoreGiveFromISR(sendReceiveSemaphore, &xHigherPriorityTaskWoken);
    return 0;
}
#endif

/**
 * @brief espnowCom_addRecv_cb()
 * 
 * function to add a callback function when data of @type is received
 * 
 * @param type          type of data the function should be used for (0 to 29)
 * @param cb_function   function that is called after data is received
 * bps: void myfn(int type, int slave, void* data, int len) (Master)
 * bsp: void myfn(int type, void *data, int len) (Slave)
 * Note: *data must not be freed in the callback function
 * @return              0 on success
 *                      1 type already connected to a function
 *                     -1 on failure
 */

int espnowCom_addRecv_cb(int type, espnowCom_user_receive_cb cb_function){
    //type not OK!!
    if(type < 0 || type >= CONFIG_ESPNOWCOM_MAX_CB_FUNCTIONS){
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
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(mac_addr == NULL){
        ESP_LOGE(TAG, "sending failed: mac_addr is NULL...");
    }
    if(status == ESP_NOW_SEND_FAIL){
        //propably offline
        ESP_LOGE(TAG, "sending failed: status failed");
    }
    xSemaphoreGiveFromISR(sendSemaphore, &xHigherPriorityTaskWoken);
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
    static uint8_t * mac_addr;
    static espnowCom_recvEvent evt;
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

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
    xSemaphoreGiveFromISR(sendReceiveSemaphore, &xHigherPriorityTaskWoken);
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
        if(xQueueReceive(recvQueue, &recvevt, ( TickType_t ) 0) == pdTRUE){

            recvstruct = (espnowCom_DataStruct_Base *) recvevt.recv_data;

            switch(recvstruct->type){
                // Protokoll managment
                case espnowCom_DataType_Mgmt:
                    mgmt_recvstruct = (espnowCom_DataStruct_Mgmt *) recvevt.recv_data;

#ifdef CONFIG_ESPNOWCOM_MASTERMODE

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
                            if(esp_SlavesNum >= CONFIG_ESPNOWCOM_MAX_SLAVES){
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
                            if(CONFIG_ESPNOWCOM_DEBUG)
                                ESP_LOGI(TAG, "keepalive signale from %s", esp_Slaves[mgmt_recvstruct->mgmt_count].name);
                            
                            esp_Slaves[mgmt_recvstruct->mgmt_count].pingResp = true;
                        }
                    }
                    free(recvevt.recv_data);
                    break;
#else
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
                            if(CONFIG_ESPNOWCOM_DEBUG)
                                ESP_LOGI(TAG, "got pinged by master");
                            memcpy(mgmt_recvstruct->name, CONFIG_ESPNOWCOM_SLAVENAME, ESPNOW_NAME_LEN);
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
                    free(recvevt.recv_data);
                    break;
#endif
                // receive user data    
                default:
                    if(CONFIG_ESPNOWCOM_DEBUG)
                        ESP_LOGI(TAG, "Received user Data");

                    recvstruct = (espnowCom_DataStruct_Base *) recvevt.recv_data;
                    type = recvstruct->type - espnowCom_DataType_Mgmt - 1 ;
                    
                    if(type < CONFIG_ESPNOWCOM_MAX_CB_FUNCTIONS){
                        //function pointer already setup
                        if(user_receive_cb_fn[type] != NULL){
#ifdef CONFIG_ESPNOWCOM_MASTERMODE
                            user_receive_cb_fn[type](type, _espnowCom_MacToSlave(recvevt.mac), (void*) recvstruct->data, recvevt.len);
#else
                            user_receive_cb_fn[type](type, (void*) recvstruct->data, recvevt.len);
#endif
                        }
                        else{
                            if(CONFIG_ESPNOWCOM_DEBUG)
                                ESP_LOGI(TAG, "no handler for received Data!");
                        }
                    }
                    free(recvevt.recv_data);
            }
        }
        // send userData
        if(xQueueReceive(sendQueue, &sendevt, ( TickType_t ) 0) == pdTRUE){
            if(CONFIG_ESPNOWCOM_DEBUG){
                recvstruct = (espnowCom_DataStruct_Base *) sendevt.send_data;
                ESP_LOGI(TAG, "sending %d, %s, %d", recvstruct->type, (char*) recvstruct->data, sendevt.len);
            }
            xSemaphoreTake(sendSemaphore, 100 / portTICK_PERIOD_MS);

            esp_now_send(sendevt.mac, (uint8_t*) sendevt.send_data, sendevt.len);
            // Todo check if it can be removed and data be freed
            //vTaskDelay(1 / portTICK_PERIOD_MS);
            free(sendevt.send_data);
        }
#ifdef CONFIG_ESPNOWCOM_MASTERMODE
        // send mngmt
        if(xTaskGetTickCount() - Ticks_lasteExec > CONFIG_ESPNOWCOM_PING_INTERVAL / portTICK_PERIOD_MS){
            
            if(broadcast){
                if(CONFIG_ESPNOWCOM_DEBUG)
                    ESP_LOGI(TAG, "sending boradcast");
                mgmt_sendstruct.type = espnowCom_DataType_Mgmt;
                mgmt_sendstruct.mgmt_type = espnowCom_MGMT_TYPE_SYN;
                memcpy(mgmt_sendstruct.payload, payload, ESPNOWCOM_PAYLOADLEN);
                memcpy(mgmt_sendstruct.name, CONFIG_ESPNOWCOM_MASTERNAME, strlen(CONFIG_ESPNOWCOM_MASTERNAME));

                xSemaphoreTake(sendSemaphore, 100 / portTICK_PERIOD_MS);

                esp_now_send(broadcast_Mac, (uint8_t* ) &mgmt_sendstruct, sizeof(espnowCom_DataStruct_Mgmt));

                Ticks_lasteExec = xTaskGetTickCount();
            }
            else{
                if(CONFIG_ESPNOWCOM_DEBUG)
                    ESP_LOGI(TAG, "sending ping");
                _espnowCom_pingSlaves(payload);
                Ticks_lasteExec = xTaskGetTickCount();
            }
        }
#endif
    //vTaskDelay(10 / portTICK_PERIOD_MS);
    // Wait for new event to arrive but max 100ms
    xSemaphoreTake(sendReceiveSemaphore, 100 / portTICK_PERIOD_MS);
    }
}
#ifdef CONFIG_ESPNOWCOM_MASTERMODE
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
    if(esp_SlavesNum < CONFIG_ESPNOWCOM_MAX_SLAVES){

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

    xSemaphoreTake(sendSemaphore, 100 / portTICK_PERIOD_MS);

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
    memcpy(ping.name, CONFIG_ESPNOWCOM_MASTERNAME, ESPNOW_NAME_LEN);
    memcpy(ping.payload, payload, ESPNOWCOM_PAYLOADLEN);

    for(int i = 0; i < esp_SlavesNum; i++){
        // check if we received ping response
        if(esp_Slaves[i].pingResp != true){
            if(CONFIG_ESPNOWCOM_DEBUG)
                ESP_LOGW(TAG, "%s disconnected", esp_Slaves[i].name);
            esp_Slaves[i].connected = false;
        }
        else{
            esp_Slaves[i].connected = true;
        }
        ping.mgmt_count = i;
        xSemaphoreTake(sendSemaphore, 100 / portTICK_PERIOD_MS);

        ret = esp_now_send(esp_Slaves[i].mac, (uint8_t *) &ping, sizeof(espnowCom_DataStruct_Mgmt));
        
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "couldn't send ping!, %x", ret);
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
#else
/**
 * @brief _espnowCom_addMaster
 * 
 *  well adds a Master mac address array
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
 * @brief _espnowCom_ACKMaster()
 * 
 * function to send ACK to Master and allow connection
 *
 */

void _espnowCom_ACKMaster(uint8_t *mac, void *payload){
    esp_err_t ret=4;
    espnowCom_DataStruct_Mgmt ack;
    
    ack.type = espnowCom_DataType_Mgmt;
    ack.mgmt_type = espnowCom_MGMT_TYPE_SYN_ACK;
    memcpy(ack.payload, payload, ESP_NOW_ETH_ALEN);
    memcpy(ack.name, CONFIG_ESPNOWCOM_SLAVENAME, ESPNOW_NAME_LEN);
    while(ret != ESP_OK){
        xSemaphoreTake(sendSemaphore, 100 / portTICK_PERIOD_MS);
        ret = esp_now_send(mac, (uint8_t *) &ack, sizeof(espnowCom_DataStruct_Mgmt));
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "couldn't send ACK! (%d)", ret);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
#endif