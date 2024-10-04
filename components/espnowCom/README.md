
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
#include "espnowCom.h"

void app_main() {
    // Initialize ESPNOW in master mode
    espnowCom_init();

    // Send data to slave
    const char* msg = "Hello from Master";
    espnowCom_send(0, 1, (void*)msg, strlen(msg));

    // Register a callback to receive data
    espnowCom_addRecv_cb(1, master_receive_callback);
}

void master_receive_callback(int type, int slave, void* data) {
    printf("Received data from slave: %s
", (char*)data);
}
```

#### In Slave Mode:

In slave mode, the device listens for data from the master and responds accordingly.

```c
#include "espnowCom.h"

void app_main() {
    // Initialize ESPNOW in slave mode
    espnowCom_init();

    // Register a callback to receive data
    espnowCom_addRecv_cb(1, slave_receive_callback);
}

void slave_receive_callback(int type, void* data, int len) {
    printf("Received data from master: %s
", (char*)data);
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

