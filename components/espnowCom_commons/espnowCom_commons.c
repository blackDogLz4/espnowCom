/* myespnow.c */
#include "espnowCom_commons.h"

// private variables
static xQueueHandle sendQueue;
static xQueueHandle recvQueue; 
static uint8_t broadcast_Mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t *sendDataBuffer;
static uint8_t *recvDataBuffer;

// mutex for state switching in Send/Recevie Tasks
static xSemaphoreHandle state_mutex;
static int state_current = espnowCom_Sate_Connect;

// private functions
static void espnowCom_send_stringTask(void *);
static void espnowCom_RecvTask(void *data);
static void _espnowCom_send_string_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void _espnowCom_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);


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
    // TODO setup send and receive calback function
    // ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    // ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );


    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    // initialize basic send functionality
    sendQueue = xQueueCreate(1, sizeof(espnowCom_sendEvent));
    if (sendQueue == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }

    // initialize basic recv functionality
    recvQueue = xQueueCreate(1, sizeof(espnowCom_recvEvent));
    if (recvQueue == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }
    // create mutex for state switching
    state_mutex = xSemaphoreCreateMutex();


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

    // reserve memory for recv buffer
    recvDataBuffer = calloc(ESPNOWCOM_MAX_DATA_LEN, sizeof(uint8_t));
    if(recvDataBuffer == NULL){
        ESP_LOGE(TAG, "couldn't reserve space for send Data!!");
        ESP_ERROR_CHECK(false);
    }

    ESP_ERROR_CHECK( esp_now_register_send_cb(_espnowCom_send_string_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(_espnowCom_recv_cb) );
    
    xTaskCreate(espnowCom_send_stringTask, "espnowCom_send_stringTask", 2048, NULL, 4, NULL);
    xTaskCreate(espnowCom_RecvTask, "espnowCom_recvTask", 2048, NULL, 4, NULL);

    return ESP_OK;
}

void espnowCom_deinit(){
    esp_now_deinit();
}
static void _espnowCom_send_string_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    espnowCom_sendEvent evt;
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }
}
static void _espnowCom_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    espnowCom_recvEvent evt;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    //set Buffer
    evt.recv_data = (espnowCom_DataStruct *) recvDataBuffer;
    memcpy(evt.mac, mac_addr, ESP_NOW_ETH_ALEN);

    evt.recv_data->type = data[0];
    memcpy(evt.recv_data->data, &data[1], len);
    
    evt.len = len;
    if (xQueueSend(recvQueue, &evt, portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
    }
}
static void espnowCom_send_stringTask(void *data){
   
    espnowCom_sendEvent evt;
    int ret;
    volatile espnowCom_States current = espnowCom_Sate_Connect;
    // send
    while(1){
        if(xSemaphoreTake(state_mutex, 1 / portTICK_PERIOD_MS)){
            current = state_current;
            vTaskDelay(10 / portTICK_PERIOD_MS);
            xSemaphoreGive(state_mutex);
        }
        switch(current){
            case espnowCom_Sate_Connect:
                if (xQueueReceive(sendQueue, &evt, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                    ESP_LOGI(TAG, "Broadcasting %s", (char* ) evt.send_data->data);
                    ret=esp_now_send(evt.mac , (uint8_t*) evt.send_data, evt.len);
                    if(ret != ESP_OK){
                        ESP_LOGE(TAG, "Sending failed, %d", ret);
                        //espnowCom_deinit();
                    }
                    //vTaskDelay(portTICK_RATE_MS);
                }
                break;
            case espnowCom_Sate_Run:
                ESP_LOGI(TAG, "Not implemented yet");
                vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }   
}
static void espnowCom_RecvTask(void *data){
   
    espnowCom_recvEvent recvevt;
    volatile espnowCom_States current = espnowCom_Sate_Connect;
    // receive
    while(1){
        if(xSemaphoreTake(state_mutex, 1 / portTICK_PERIOD_MS)){
            current = state_current;
            vTaskDelay(10 / portTICK_PERIOD_MS);
            xSemaphoreGive(state_mutex);
        }
        switch(current){
            case espnowCom_Sate_Connect:
                if (xQueueReceive(recvQueue, &recvevt, 100 / portTICK_PERIOD_MS) == pdTRUE){
                    ESP_LOGI(TAG, "received %s", recvevt.recv_data->data);
                    ESP_LOGI(TAG, "Connect");
                }
                break;
            case espnowCom_Sate_Run:
                if (xQueueReceive(recvQueue, &recvevt, 100 / portTICK_PERIOD_MS) == pdTRUE){
                    ESP_LOGI(TAG, "received %s", recvevt.recv_data->data);
                    ESP_LOGI(TAG, "Run");
                }
            break;
        vTaskDelay(1 / portTICK_PERIOD_MS);
        }
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

void espnowCom_send_string(char *str){
    static espnowCom_sendEvent evt = { 0 };
    
    // broadcast string
    memcpy((void *)evt.mac, (void *)broadcast_Mac, ESP_NOW_ETH_ALEN);

    evt.len = strlen(str) + 1;
    
    if(evt.len > ESPNOWCOM_MAX_DATA_LEN){
        ESP_LOGE(TAG, "Too much Data to send!!");
        return;
    }

    evt.send_data = (espnowCom_DataStruct *) sendDataBuffer;
    evt.send_data->type = espnowCom_Data_Type_String;
    memcpy((void *)evt.send_data->data, (void *) str, evt.len - 1); 

    xQueueSend(sendQueue, &evt, portMAX_DELAY);
}