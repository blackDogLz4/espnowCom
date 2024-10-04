#include "nvs_flash.h"
#include "espnowCom.h"

#define TAG "Main"

void recv_handler(int type, int slave, void *data, int len){
    char *str;
    str = (char*) data;
    ESP_LOGI(TAG, "from %d, %s", slave, str);
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // Initialize espnowCom
    espnowCom_init();

    vTaskDelay(100 / portTICK_PERIOD_MS);

    // add receiv handler
    espnowCom_addRecv_cb(1, &recv_handler);
    while(1){
        // send HALLO to slave 0 every second
        espnowCom_send(0, 0, (void *)"HALLO\n", 10);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
