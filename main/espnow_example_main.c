#include "nvs_flash.h"
#include "espnowCom.h"

#define TAG "Main"

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

    while(1){
        espnowCom_send(0, 0, (void *)"ON", 3);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        espnowCom_send(0, 0, (void *)"OFF", 4);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
