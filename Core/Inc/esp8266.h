#ifndef INC_ESP8266_H_
#define INC_ESP8266_H_

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define WIFI_SSID     "Ten_WiFi_Nha_Ban"
#define WIFI_PASS     "Mat_Khau_WiFi"
#define SERVER_IP     "api.thingspeak.com"
#define API_KEY       "YOUR_WRITE_API_KEY"

void ESP8266_Init(UART_HandleTypeDef *huart);
void ESP8266_SendLog(uint8_t door_event, uint8_t alert_event);
bool ESP8266_CheckRemoteUnlock(void);

#endif /* INC_ESP8266_H_ */
