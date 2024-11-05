#include "nvs_flash.h"
#include "espnowCom.h"
#include "driver/gpio.h"

#define TAG "Main"

void app_main(void)
{
    // Initialize NVS
    int i=0;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    //Config Buildin LED
    gpio_config_t Led = {};
    Led.intr_type = GPIO_INTR_DISABLE;
    Led.pin_bit_mask = 1<<2;
    Led.mode = GPIO_MODE_OUTPUT;

    gpio_config(&Led);

    // Initialize espnowCom
    espnowCom_init();

    vTaskDelay(100 / portTICK_PERIOD_MS);

    while(1){
        gpio_set_level(2,1);
        while(espnowCom_send(0, 0, (void *)"ON", 3) != 0){
            ESP_LOGE(TAG, "error sending on");
            vTaskDelay(1 / portTICK_PERIOD_MS);
            i++;
            if(i >= 5){
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        }
        i=0;
        vTaskDelay(10 / portTICK_PERIOD_MS);
        
        gpio_set_level(2,0);
        while(espnowCom_send(0, 0, (void *)"OFF", 4) != 0){
            ESP_LOGE(TAG, "error sending off");
            vTaskDelay(1 / portTICK_PERIOD_MS);
            i++;
            if(i >= 5){
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        }
        i=0;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
