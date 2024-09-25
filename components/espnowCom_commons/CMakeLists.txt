idf_component_register(SRCS "espnowCom_commons.c"
                  INCLUDE_DIRS "."
                  REQUIRES esp_event esp_netif esp_wifi esp_system)