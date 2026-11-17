#include "esp8266.h"
#include <string.h>
#include <stdio.h>

static char wifi_token[17] = "KHOA9981";

static UART_HandleTypeDef *esp_uart;
static uint8_t uart_rx_byte;
static char uart_rx_buf[80];
static uint8_t uart_rx_idx = 0;

static volatile RemoteCmd_t pending_cmd = REMOTE_CMD_NONE;
static char remote_new_pass[8] = {0};

// Biến chống dò mã qua WiFi
static uint8_t wifi_wrong_count = 0;
static uint32_t wifi_lockout_until = 0;

static void ESP8266_SendCmd(const char *cmd) {
    HAL_UART_Transmit(esp_uart, (uint8_t*)cmd, strlen(cmd), 200);
}

static void ESP8266_FlushRx(void) {
    __HAL_UART_CLEAR_OREFLAG(esp_uart);
    uint8_t dummy;
    while (HAL_UART_Receive(esp_uart, &dummy, 1, 5) == HAL_OK);
}

static uint16_t ESP8266_ReadResp(char *buf, uint16_t max_len, uint32_t timeout_ms) {
    uint16_t idx = 0;
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start < timeout_ms) && (idx < max_len - 1)) {
        uint8_t ch;
        if (HAL_UART_Receive(esp_uart, &ch, 1, 10) == HAL_OK) {
            if (ch >= 32 && ch <= 126) {
                buf[idx++] = ch;
            } else {
                buf[idx++] = ' ';
            }
        }
    }
    buf[idx] = '\0';
    return idx;
}

bool ESP8266_Init(UART_HandleTypeDef *huart, char *ip_out, char *debug_msg) {
    esp_uart = huart;
    if (ip_out != NULL) strcpy(ip_out, "0.0.0.0");
    if (debug_msg != NULL) strcpy(debug_msg, "Unknown");

    char resp[160] = {0};

    // BƯỚC 1: Thoát trạng thái kẹt dữ liệu (nếu có) và tự động dò Baudrate
    ESP8266_SendCmd("+++");
    HAL_Delay(1100);
    ESP8266_SendCmd("\r\n");
    HAL_Delay(200);

    ESP8266_FlushRx();
    ESP8266_SendCmd("AT\r\n");
    uint16_t len = ESP8266_ReadResp(resp, sizeof(resp), 800);

    if (len == 0 || strstr(resp, "OK") == NULL) {
        // Thử hạ xuống 9600 nếu 115200 không trả về OK
        HAL_UART_DeInit(esp_uart);
        esp_uart->Init.BaudRate = 9600;
        HAL_UART_Init(esp_uart);

        ESP8266_FlushRx();
        ESP8266_SendCmd("AT\r\n");
        len = ESP8266_ReadResp(resp, sizeof(resp), 800);
    }

    if (len == 0) {
        if (debug_msg) strcpy(debug_msg, "Loi: Khong phan hoi");
        return false;
    }
    if (strstr(resp, "OK") == NULL) {
        if (debug_msg) snprintf(debug_msg, 22, "Sai Baud:%.12s", resp);
        return false;
    }

    // BƯỚC 2: Đặt chế độ Station và kết nối WiFi
    ESP8266_SendCmd("AT+CWMODE=1\r\n");
    HAL_Delay(300);

    ESP8266_FlushRx();
    char wifi_cmd[128];
    snprintf(wifi_cmd, sizeof(wifi_cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    ESP8266_SendCmd(wifi_cmd);

    memset(resp, 0, sizeof(resp));
    uint16_t idx = 0;
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 8000) {
        uint8_t ch;
        if (HAL_UART_Receive(esp_uart, &ch, 1, 20) == HAL_OK) {
            if (idx < sizeof(resp) - 1) {
                resp[idx++] = (ch >= 32 && ch <= 126) ? ch : ' ';
                resp[idx] = '\0';
            }
            if (strstr(resp, "WIFI GOT IP") != NULL || strstr(resp, "OK") != NULL) {
                break;
            }
            if (strstr(resp, "FAIL") != NULL || strstr(resp, "ERROR") != NULL) {
                if (debug_msg) {
                    char *p_err = strstr(resp, "CWJAP:");
                    if (p_err) snprintf(debug_msg, 22, "Loi WiFi: %.11s", p_err);
                    else strcpy(debug_msg, "WiFi FAIL (5G/MK?)");
                }
                return false;
            }
        }
    }

    // BƯỚC 3: Lấy địa chỉ IP (AT+CIFSR)
    ESP8266_FlushRx();
    ESP8266_SendCmd("AT+CIFSR\r\n");
    ESP8266_ReadResp(resp, sizeof(resp), 1000);

    // Bật Server cổng 80
    ESP8266_SendCmd("AT+CIPMUX=1\r\n");
    HAL_Delay(200);
    ESP8266_SendCmd("AT+CIPSERVER=1,80\r\n");
    HAL_Delay(200);

    // Bật ngắt nhận lệnh từ xa
    HAL_UART_Receive_IT(esp_uart, &uart_rx_byte, 1);

    char *p_ip = strstr(resp, "STAIP,\"");
    if (p_ip != NULL) {
        p_ip += 7;
        char *p_end = strchr(p_ip, '"');
        if (p_end != NULL && (p_end - p_ip) < 16) {
            char temp_ip[16] = {0};
            memcpy(temp_ip, p_ip, p_end - p_ip);
            if (strcmp(temp_ip, "0.0.0.0") != 0 && strlen(temp_ip) >= 7) {
                if (ip_out != NULL) strcpy(ip_out, temp_ip);
                if (debug_msg != NULL) strcpy(debug_msg, "OK");
                return true;
            }
        }
    }

    if (debug_msg) snprintf(debug_msg, 22, "Raw:%.17s", resp);
    return false;
}

void ESP8266_SendLog(uint8_t door_event, uint8_t alert_event) {
    char tcp_cmd[64];
    char http_req[160];
    char send_len[32];

    snprintf(tcp_cmd, sizeof(tcp_cmd), "AT+CIPSTART=4,\"TCP\",\"%s\",80\r\n", SERVER_IP);
    ESP8266_SendCmd(tcp_cmd);
    HAL_Delay(300);

    snprintf(http_req, sizeof(http_req),
             "GET /update?api_key=%s&field1=%d&field2=%d\r\n",
             API_KEY, door_event, alert_event);

    snprintf(send_len, sizeof(send_len), "AT+CIPSEND=4,%d\r\n", (int)strlen(http_req));
    ESP8266_SendCmd(send_len);
    HAL_Delay(100);
    ESP8266_SendCmd(http_req);
    HAL_Delay(400);
    ESP8266_SendCmd("AT+CIPCLOSE=4\r\n");
}

RemoteCmd_t ESP8266_GetRemoteCmd(char *new_pass_out) {
    RemoteCmd_t cmd = pending_cmd;
    if (cmd != REMOTE_CMD_NONE) {
        if ((cmd == REMOTE_CMD_CHANGE_PASS || cmd == REMOTE_CMD_CHANGE_MPIN) && new_pass_out != NULL) {
            strcpy(new_pass_out, remote_new_pass);
        }
        pending_cmd = REMOTE_CMD_NONE;
    }
    return cmd;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == esp_uart->Instance) {
        if (uart_rx_byte == '\n' || uart_rx_idx >= sizeof(uart_rx_buf) - 1) {
            uart_rx_buf[uart_rx_idx] = '\0';

            // Chỉ kiểm tra gói tin từ client gửi tới (+IPD)
            if (strstr(uart_rx_buf, "+IPD") != NULL) {
            	if (strstr(uart_rx_buf, "+IPD") != NULL) {
            	                if (HAL_GetTick() >= wifi_lockout_until) {
            	                    char cmd_prefix[32];

            	                    // 1. Kiểm tra lệnh đổi chính Token: <TOKEN_CŨ>_TOKEN=<TOKEN_MỚI>
            	                    snprintf(cmd_prefix, sizeof(cmd_prefix), "%s_TOKEN=", wifi_token);
            	                    char *p_tok = strstr(uart_rx_buf, cmd_prefix);
            	                    if (p_tok != NULL) {
            	                        char *new_tok = p_tok + strlen(cmd_prefix);
            	                        // Lấy chuỗi Token mới đến trước ký tự xuống dòng \r hoặc \n
            	                        uint8_t t_len = strcspn(new_tok, "\r\n ");
            	                        if (t_len >= 4 && t_len <= 16) {
            	                            memcpy(wifi_token, new_tok, t_len);
            	                            wifi_token[t_len] = '\0';
            	                            wifi_wrong_count = 0;
            	                            // Báo phản hồi nhẹ qua còi/LED bằng cách tái sử dụng lệnh đổi MPIN hoặc tự gửi ACK
            	                        }
            	                    }
            	                    // 2. Các lệnh điều khiển dùng wifi_token hiện tại
            	                    else {
            	                        snprintf(cmd_prefix, sizeof(cmd_prefix), "%s_PASS=", wifi_token);
            	                        char *p_pass = strstr(uart_rx_buf, cmd_prefix);

            	                        snprintf(cmd_prefix, sizeof(cmd_prefix), "%s_MPIN=", wifi_token);
            	                        char *p_mpin = strstr(uart_rx_buf, cmd_prefix);

            	                        char cmd_open[24], cmd_lock[24], cmd_mode[24];
            	                        snprintf(cmd_open, sizeof(cmd_open), "%s_OPEN", wifi_token);
            	                        snprintf(cmd_lock, sizeof(cmd_lock), "%s_LOCK", wifi_token);
            	                        snprintf(cmd_mode, sizeof(cmd_mode), "%s_MODE", wifi_token);

            	                        if (p_pass != NULL && strlen(p_pass) >= strlen(wifi_token) + 10) {
            	                            memcpy(remote_new_pass, p_pass + strlen(wifi_token) + 6, 4);
            	                            remote_new_pass[4] = '\0';
            	                            pending_cmd = REMOTE_CMD_CHANGE_PASS;
            	                            wifi_wrong_count = 0;
            	                        }
            	                        else if (p_mpin != NULL && strlen(p_mpin) >= strlen(wifi_token) + 10) {
            	                            memcpy(remote_new_pass, p_mpin + strlen(wifi_token) + 6, 4);
            	                            remote_new_pass[4] = '\0';
            	                            pending_cmd = REMOTE_CMD_CHANGE_MPIN;
            	                            wifi_wrong_count = 0;
            	                        }
            	                        else if (strstr(uart_rx_buf, cmd_open) != NULL) {
            	                            pending_cmd = REMOTE_CMD_OPEN;
            	                            wifi_wrong_count = 0;
            	                        }
            	                        else if (strstr(uart_rx_buf, cmd_lock) != NULL) {
            	                            pending_cmd = REMOTE_CMD_LOCK;
            	                            wifi_wrong_count = 0;
            	                        }
            	                        else if (strstr(uart_rx_buf, cmd_mode) != NULL) {
            	                            pending_cmd = REMOTE_CMD_TOGGLE_MODE;
            	                            wifi_wrong_count = 0;
            	                        }
            	                        else {
            	                            wifi_wrong_count++;
            	                            if (wifi_wrong_count >= 3) {
            	                                wifi_lockout_until = HAL_GetTick() + 300000;
            	                                wifi_wrong_count = 0;
            	                            }
            	                        }
            	                    }
            	                }
            	            }
            }

            uart_rx_idx = 0;
        } else {
            uart_rx_buf[uart_rx_idx++] = uart_rx_byte;
        }
        HAL_UART_Receive_IT(esp_uart, &uart_rx_byte, 1);
    }
}
