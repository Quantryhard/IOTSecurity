#ifndef INC_ESP8266_H_
#define INC_ESP8266_H_

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

// Cấu hình WiFi và Server của bạn (Giữ nguyên thông tin mạng của bạn nếu khác)
#ifndef WIFI_SSID
#define WIFI_SSID "WifiNB"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "88886666"
#endif

#ifndef SERVER_IP
#define SERVER_IP "api.thingspeak.com"
#endif

#ifndef API_KEY
#define API_KEY "YOUR_THINGSPEAK_API_KEY"
#endif

typedef enum {
    REMOTE_CMD_NONE = 0,
    REMOTE_CMD_OPEN,
    REMOTE_CMD_LOCK,
    REMOTE_CMD_TOGGLE_MODE,
    REMOTE_CMD_CHANGE_PASS,
    REMOTE_CMD_CHANGE_MPIN
} RemoteCmd_t;

bool ESP8266_Init(UART_HandleTypeDef *huart, char *ip_out, char *debug_msg);
void ESP8266_SendLog(uint8_t door_event, uint8_t alert_event);
RemoteCmd_t ESP8266_GetRemoteCmd(char *new_pass_out);

#endif /* INC_ESP8266_H_ */
