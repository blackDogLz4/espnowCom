
# ESPNOW Communication Component

## Overview

This component provides an easy-to-use interface for wireless communication between ESP32 devices using the ESP-NOW protocol. It supports both **Master** and **Slave** modes, allowing you to send and receive data with minimal overhead.

In addition, the module offers **connection handling**, a **ping mechanism** to check if the slave is still online, and **automatic reconnection** for devices that disconnect.

### Features:
- **Master-Slave Communication**: Supports both master and slave roles.
- **Low overhead and low power**: ESP-NOW minimizes power consumption.
- **Wi-Fi Mode Configuration**: Station or SoftAP mode options.
- **Long-range mode**: Optionally enable extended communication range.
- **Automatic Reconnection**: Auto-reconnect for devices that go offline.
- **Ping Mechanism**: Check the availability of connected devices.
- **Callback Support**: User-defined callbacks for handling received data.

## Instructions to include the component in your project

### 1. Clone the Component into Your Project

Navigate to the `components` directory of your project, then clone the ESPNOW component:

```bash
cd <your esp-idf project folder>
git clone https://github.com/blackDogLz4/espnow/tree/main
```

This will copy the component into your project's component folder.

### 2. Configure the Component

Open the ESP-IDF configuration menu to configure your ESPNOW communication settings:

```bash
idf.py menuconfig
```

Navigate to the **Component Config** and **ESPNOW Communication Component** to configure settings like Wi-Fi mode, long-range mode, and whether the device will function as a Master or Slave.

### 3. Using the Component

Once the component is set up and configured, include it in your code by calling the appropriate functions:

#### In Master Mode:

In master mode, you can send data to slaves, ping slaves to check if they are online, and register callbacks for receiving data.

```c
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

```

#### In Slave Mode:

In slave mode, the device listens for data from the master and responds accordingly.

```c
#include "nvs_flash.h"
#include "espnowCom.h"

#define TAG "Main-Slave"

void handlerfunc(int type, void *data, int len){
    char* str;
    str = (char* )data;

    ESP_LOGI(TAG, "received %s", str);
}

void app_main(void)
{
 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    
    espnowCom_init();

     
    // temporary add later slaves connected function or something like that
    vTaskDelay(100 / portTICK_PERIOD_MS);
    espnowCom_addRecv_cb(0, &handlerfunc);
    while(1){
        espnowCom_send(1, "Hey", 4);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

```

### 4. Build and Flash

After setting up the component and adding it to your project, use the following commands to build and flash your project:

```bash
idf.py build
idf.py flash
```

Make sure both Master and Slave devices are set up and communicating using the ESP-NOW protocol.

## License

This component is licensed under [Your License Here].

