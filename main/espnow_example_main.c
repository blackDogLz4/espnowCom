/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "espnow_example.h"
#include "espnowCom_Master.h"

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

    espnowCom_init();

    // while(1){

    //         for(int i = 0; i<5; i++){
    //             ESP_LOGI(TAG, "request send Hey");
    //             espnowCom_master_send_string(0, "Hey");
    //             vTaskDelay(50 / portTICK_PERIOD_MS);  // Small delay after sending string
    //             espnowCom_master_send_float(0, 0.05);
    //             vTaskDelay(1000 / portTICK_PERIOD_MS);
    //         }
    //         // ESP_LOGW(TAG, "switch mode");
    //         // espnowCom_switchMode(espnowCom_Sate_Run);
    //         // vTaskDelay(5000/portTICK_PERIOD_MS);
    //         // espnowCom_switchMode(espnowCom_Sate_Connect);
    // }
}
