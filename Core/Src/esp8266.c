#include "esp8266.h"
#include <string.h>
#include <stdio.h>

static UART_HandleTypeDef *esp_uart;
static uint8_t uart_rx_byte;
static char uart_rx_buf[64];
static uint8_t uart_rx_idx = 0;
static volatile bool remote_unlock_flag = false;

static void ESP8266_SendCmd(const char *cmd) {
    HAL_UART_Transmit(esp_uart, (uint8_t*)cmd, strlen(cmd), 200);
}

void ESP8266_Init(UART_HandleTypeDef *huart) {
    esp_uart = huart;
    ESP8266_SendCmd("AT+RST\r\n");
    HAL_Delay(1500);
    ESP8266_SendCmd("AT+CWMODE=1\r\n");
    HAL_Delay(500);

    char wifi_cmd[128];
    snprintf(wifi_cmd, sizeof(wifi_cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    ESP8266_SendCmd(wifi_cmd);
    HAL_Delay(4000);

    HAL_UART_Receive_IT(esp_uart, &uart_rx_byte, 1);
}

void ESP8266_SendLog(uint8_t door_event, uint8_t alert_event) {
    char tcp_cmd[64];
    char http_req[160];
    char send_len[32];

    snprintf(tcp_cmd, sizeof(tcp_cmd), "AT+CIPSTART=\"TCP\",\"%s\",80\r\n", SERVER_IP);
    ESP8266_SendCmd(tcp_cmd);
    HAL_Delay(300);

    snprintf(http_req, sizeof(http_req),
             "GET /update?api_key=%s&field1=%d&field2=%d\r\n",
             API_KEY, door_event, alert_event);

    snprintf(send_len, sizeof(send_len), "AT+CIPSEND=%d\r\n", (int)strlen(http_req));
    ESP8266_SendCmd(send_len);
    HAL_Delay(100);
    ESP8266_SendCmd(http_req);
}

bool ESP8266_CheckRemoteUnlock(void) {
    if (remote_unlock_flag) {
        remote_unlock_flag = false;
        return true;
    }
    return false;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == esp_uart->Instance) {
        if (uart_rx_byte == '\n' || uart_rx_idx >= sizeof(uart_rx_buf) - 1) {
            uart_rx_buf[uart_rx_idx] = '\0';
            if (strstr(uart_rx_buf, "OPEN") != NULL) {
                remote_unlock_flag = true;
            }
            uart_rx_idx = 0;
        } else {
            uart_rx_buf[uart_rx_idx++] = uart_rx_byte;
        }
        HAL_UART_Receive_IT(esp_uart, &uart_rx_byte, 1);
    }
}