/* myespnow.c */
#include "espnowCom_commons.h"

// private variables
static xQueueHandle sendQueue;
static xQueueHandle recvQueue; 
static uint8_t broadcast_Mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// private functions
static void espnowCom_Task(void *);
static void _espnowCom_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
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
    sendQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnowCom_SendEvent));
    if (sendQueue == NULL) {
        ESP_LOGE(TAG, "Create Quee fail");
        return ESP_FAIL;
    }

    // initialize basic recv functionality
    recvQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnowCom_RecvEvent));
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

    ESP_ERROR_CHECK( esp_now_register_send_cb(_espnowCom_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(_espnowCom_recv_cb) );
    
    xTaskCreate(espnowCom_Task, "espnowCom_Task", 2048, NULL, 4, NULL);

    return ESP_OK;
}

void espnowCom_deinit(){
    esp_now_deinit();
}
static void _espnowCom_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    espnowCom_SendEvent evt;
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }
}
static void _espnowCom_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    espnowCom_RecvEvent evt;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }
    ESP_LOGI(TAG, "received somtehing");
    memcpy(evt.mac, mac_addr, ESP_NOW_ETH_ALEN);
    memcpy(evt.message, data, len);
    evt.len = len;
    if (xQueueSend(recvQueue, &evt, portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
    }
}
static void espnowCom_Task(void *data){
   
    espnowCom_SendEvent evt;
    espnowCom_RecvEvent recvevt;
    int ret;
    // send
    while(1){
        // while (xQueueReceive(sendQueue, &evt, portMAX_DELAY) == pdTRUE) {
        //     ESP_LOGI(TAG, "Broadcasting %s", evt.message);
        //     ret=esp_now_send(broadcast_Mac, evt.message, evt.len);
        //     if(ret != ESP_OK){
        //         ESP_LOGE(TAG, "Sending failed, %d", ret);
        //         //espnowCom_deinit();
        //     }
        //     //vTaskDelay(portTICK_RATE_MS);
        // }

        // recv quee
        while (xQueueReceive(recvQueue, &recvevt, portMAX_DELAY) == pdTRUE){
            ESP_LOGI(TAG, "received %s", recvevt.message);
        }
    }   
}

void espnowCom_send(char *str){
    espnowCom_SendEvent evt;

    evt.len = strlen(str)+1;
    strcpy((char *) evt.message, str); 

    xQueueSend(sendQueue, &evt, portMAX_DELAY);
}