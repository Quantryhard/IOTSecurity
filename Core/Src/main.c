#include "main.h"
#include "ssd1306.h"
#include "rc522.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Các biến ngoại vi do CubeMX tạo */
I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;

/* Cấu hình WiFi & Server IoT (ThingSpeak / Webhook) */
#define WIFI_SSID     "Ten_WiFi_Nha_Ban"
#define WIFI_PASS     "Mat_Khau_WiFi"
#define SERVER_IP     "api.thingspeak.com"
#define API_KEY       "YOUR_WRITE_API_KEY"

/* Cấu hình hệ thống bảo mật */
#define MAX_CARDS     5
#define PASS_LEN      4

char current_pin[PASS_LEN + 1] = "1234";
char input_pin[PASS_LEN + 1]   = "";
uint8_t pin_idx = 0;

// Danh sách thẻ RFID: Thẻ đầu tiên [0] mặc định là thẻ MASTER
uint8_t master_uid[4] = {0x12, 0x34, 0x56, 0x78}; // Thay bằng UID thẻ trắng của bạn
uint8_t valid_cards[MAX_CARDS][4] = {
    {0x12, 0x34, 0x56, 0x78},
    {0xAA, 0xBB, 0xCC, 0xDD},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
};

/* Biến trạng thái hệ thống (State Machine) */
typedef enum {
    STATE_NORMAL = 0,
    STATE_UNLOCKED,
    STATE_LOCKED_OUT,
    STATE_CHANGE_PASS_OLD,
    STATE_CHANGE_PASS_NEW,
    STATE_RFID_ADMIN_WAIT_MASTER,
    STATE_RFID_ADMIN_SCAN_CARD
} SystemState_t;

SystemState_t sys_state = STATE_NORMAL;

bool away_mode = false;         // Chế độ vắng nhà (giám sát PIR)
bool alarm_active = false;      // Đang hú còi báo động
uint8_t wrong_attempts = 0;     // Đếm số lần sai (Chống Brute-force)

uint32_t unlock_timer = 0;      // Đếm ngược 5s tự đóng chốt
uint32_t lockout_timer = 0;     // Đếm ngược 60s đóng băng hệ thống
uint32_t alarm_blink_timer = 0; // Nháy đèn LED khi báo động
uint32_t rfid_poll_timer = 0;   // Chu kỳ quét RFID

/* Biến nhận lệnh từ xa qua ESP8266 UART */
uint8_t uart_rx_byte;
char uart_rx_buf[64];
uint8_t uart_rx_idx = 0;
volatile bool remote_unlock_flag = false;

/* Prototype các hàm tự động sinh của CubeMX */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);

/* ================= HÀM ĐIỀU KHIỂN PHẦN CỨNG ================= */

void Servo_SetAngle(uint8_t angle) {
    if (angle > 180) angle = 180;
    // 0 độ = 500us, 180 độ = 2500us
    uint32_t pulse = 500 + ((uint32_t)angle * 2000) / 180;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse);
}

void Buzzer_Beep(uint8_t times, uint16_t duration_ms) {
    for (uint8_t i = 0; i < times; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
        HAL_Delay(duration_ms);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
        if (i < times - 1) HAL_Delay(80);
    }
}

void UI_UpdateScreen(const char *line1, const char *line2, const char *line3) {
    SSD1306_Clear();
    SSD1306_SetCursor(0, 0);
    SSD1306_WriteString(away_mode ? "[CHE DO: VANG NHA]" : "[CHE DO: O NHA]");
    SSD1306_SetCursor(0, 2);
    SSD1306_WriteString(line1);
    SSD1306_SetCursor(0, 4);
    SSD1306_WriteString(line2);
    SSD1306_SetCursor(0, 6);
    SSD1306_WriteString(line3);
    SSD1306_UpdateScreen();
}

/* ================= GIAO TIẾP IOT ESP8266 (AT COMMANDS) ================= */

void ESP8266_SendCmd(const char *cmd) {
    HAL_UART_Transmit(&huart1, (uint8_t*)cmd, strlen(cmd), 200);
}

void ESP8266_Init(void) {
    UI_UpdateScreen("Dang ket noi WiFi...", WIFI_SSID, "Vui long cho...");
    ESP8266_SendCmd("AT+RST\r\n");
    HAL_Delay(1500);
    ESP8266_SendCmd("AT+CWMODE=1\r\n");
    HAL_Delay(500);

    char wifi_cmd[128];
    snprintf(wifi_cmd, sizeof(wifi_cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    ESP8266_SendCmd(wifi_cmd);
    HAL_Delay(4000);
}

// Gửi log sự kiện lên Server IoT (Field1: Trạng thái cửa, Field2: Cảnh báo an ninh)
void IoT_SendLog(uint8_t door_event, uint8_t alert_event) {
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

// Ngắt nhận UART từ ESP8266 (Hỗ trợ UC10: Mở cửa từ xa khi nhận chuỗi "OPEN")
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        if (uart_rx_byte == '\n' || uart_rx_idx >= sizeof(uart_rx_buf) - 1) {
            uart_rx_buf[uart_rx_idx] = '\0';
            if (strstr(uart_rx_buf, "OPEN") != NULL) {
                remote_unlock_flag = true;
            }
            uart_rx_idx = 0;
        } else {
            uart_rx_buf[uart_rx_idx++] = uart_rx_byte;
        }
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
    }
}

/* ================= QUÉT BÀN PHÍM MA TRẬN 4X4 ================= */

char Keypad_GetKey(void) {
    const char keymap[4][4] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'}
    };
    const uint16_t row_pins[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};

    for (uint8_t r = 0; r < 4; r++) {
        // Kéo lần lượt từng hàng xuống mức LOW
        for (uint8_t i = 0; i < 4; i++) {
            HAL_GPIO_WritePin(GPIOB, row_pins[i], (i == r) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        HAL_Delay(2);

        char pressed = 0;
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET) pressed = keymap[r][0];
        else if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_RESET) pressed = keymap[r][1];
        else if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_RESET) pressed = keymap[r][2];
        else if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET) pressed = keymap[r][3];

        if (pressed != 0) {
            // Chống dội phím & chờ nhả phím
            HAL_Delay(20);
            while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET ||
                   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_RESET ||
                   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_RESET ||
                   HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET);
            return pressed;
        }
    }
    return 0;
}

/* ================= LOGIC XỬ LÝ KHÓA CỬA & THẺ RFID ================= */

void Door_Unlock(const char *method_msg, uint8_t log_code) {
    Servo_SetAngle(90); // Quay 90 độ mở chốt
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);   // Bật LED Xanh
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); // Tắt LED Đỏ

    // Mở cửa hợp lệ tự động tắt chế độ Vắng nhà & Báo động
    away_mode = false;
    alarm_active = false;
    wrong_attempts = 0;
    pin_idx = 0;
    memset(input_pin, 0, sizeof(input_pin));

    Buzzer_Beep(1, 120);
    UI_UpdateScreen("MO CUA THANH CONG", method_msg, "Tu dong khoa sau 5s");
    IoT_SendLog(log_code, 0);

    sys_state = STATE_UNLOCKED;
    unlock_timer = HAL_GetTick();
}

void Door_Lock(void) {
    Servo_SetAngle(0); // Quay về 0 độ khóa chốt
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
    sys_state = STATE_NORMAL;
    UI_UpdateScreen("CUA DA KHOA", "Quet the hoac nhap PIN", "A:Vang nha | B:Doi MK");
}

void Handle_AuthFailure(const char *reason) {
    wrong_attempts++;
    pin_idx = 0;
    memset(input_pin, 0, sizeof(input_pin));
    Buzzer_Beep(3, 80);

    if (wrong_attempts >= 3) {
        // UC8: Đóng băng hệ thống 60 giây chống Brute-force
        sys_state = STATE_LOCKED_OUT;
        lockout_timer = HAL_GetTick();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        UI_UpdateScreen("CANH BAO BAO MAT!", "Sai qua 3 lan!", "Khoa ban phim 60s");
        IoT_SendLog(0, 2); // Mã 2: Cảnh báo dò mật khẩu/thẻ
    } else {
        char attempt_str[24];
        snprintf(attempt_str, sizeof(attempt_str), "Con %d lan thu!", 3 - wrong_attempts);
        UI_UpdateScreen(reason, attempt_str, "Vui long thu lai");
    }
}

bool RFID_IsAuthorized(uint8_t *uid) {
    for (uint8_t i = 0; i < MAX_CARDS; i++) {
        if (memcmp(uid, valid_cards[i], 4) == 0) return true;
    }
    return false;
}

void RFID_ToggleCard(uint8_t *uid) {
    if (memcmp(uid, master_uid, 4) == 0) {
        UI_UpdateScreen("LOI THAO TAC", "Khong the xoa the", "Master!");
        Buzzer_Beep(2, 100);
        return;
    }
    // Nếu thẻ đã tồn tại -> Xóa thẻ
    for (uint8_t i = 1; i < MAX_CARDS; i++) {
        if (memcmp(uid, valid_cards[i], 4) == 0) {
            memset(valid_cards[i], 0, 4);
            Buzzer_Beep(2, 100);
            UI_UpdateScreen("QUAN LY THE RFID", "Da XOA the khoi", "bo nho he thong!");
            HAL_Delay(1500);
            Door_Lock();
            return;
        }
    }
    // Nếu thẻ chưa có -> Thêm vào ô trống
    for (uint8_t i = 1; i < MAX_CARDS; i++) {
        uint8_t empty[4] = {0, 0, 0, 0};
        if (memcmp(valid_cards[i], empty, 4) == 0) {
            memcpy(valid_cards[i], uid, 4);
            Buzzer_Beep(1, 200);
            UI_UpdateScreen("QUAN LY THE RFID", "Da THEM the moi", "thanh cong!");
            HAL_Delay(1500);
            Door_Lock();
            return;
        }
    }
    UI_UpdateScreen("BO NHO DAY", "Toi da 5 the RFID", "");
    HAL_Delay(1500);
    Door_Lock();
}

/* ================= VÒNG LẶP CHÍNH ================= */

int main(void) {
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_SPI1_Init();
    MX_TIM2_Init();
    MX_USART1_UART_Init();

    // Khởi động PWM cho Servo và đưa về góc đóng 0 độ
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    Servo_SetAngle(0);

    // Khởi động OLED, RFID và ESP8266
    SSD1306_Init(&hi2c1);
    RC522_Init(&hspi1);
    ESP8266_Init();

    // Bật ngắt nhận UART từ ESP8266
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);

    Door_Lock();

    while (1) {
        uint32_t now = HAL_GetTick();

        // 1. Kiểm tra lệnh mở cửa từ xa qua IoT (UC10)
        if (remote_unlock_flag) {
            remote_unlock_flag = false;
            Door_Unlock("Mo khoa tu xa IoT", 3);
        }

        // 2. Xử lý các trạng thái thời gian (Auto-lock 5s & Khóa bàn phím 60s)
        if (sys_state == STATE_UNLOCKED) {
            if (now - unlock_timer >= 5000) {
                Door_Lock(); // UC3: Tự động đóng chốt sau 5s
            }
            continue;
        }

        if (sys_state == STATE_LOCKED_OUT) {
            if (now - lockout_timer >= 60000) {
                wrong_attempts = 0;
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
                Door_Lock();
            }
            continue;
        }

        // 3. Giám sát cảm biến chuyển động PIR khi bật chế độ "Vắng nhà" (UC6 & UC7)
        if (away_mode) {
            if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_SET && !alarm_active) {
                alarm_active = true;
                UI_UpdateScreen("BAO DONG DOT NHAP!", "Phat hien chuyen dong", "Dang gui canh bao...");
                IoT_SendLog(0, 1); // UC11: Gửi cảnh báo khẩn cấp lên Server
            }
        }

        // Hú còi và chớp LED đỏ liên tục nếu đang có báo động
        if (alarm_active) {
            if (now - alarm_blink_timer >= 200) {
                alarm_blink_timer = now;
                HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_8);  // Nháy LED Đỏ
                HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_11); // Kêu Buzzer ngắt quãng
            }
        }

        // 4. Quét thẻ RFID mỗi 200ms (UC1 & UC5)
        if (now - rfid_poll_timer >= 200) {
            rfid_poll_timer = now;
            uint8_t card_uid[5] = {0};
            if (RC522_Check(card_uid) == RC522_OK) {
                if (sys_state == STATE_RFID_ADMIN_WAIT_MASTER) {
                    if (memcmp(card_uid, master_uid, 4) == 0) {
                        Buzzer_Beep(1, 100);
                        sys_state = STATE_RFID_ADMIN_SCAN_CARD;
                        UI_UpdateScreen("XAC THUC MASTER OK", "Quet the can THEM", "hoac XOA...");
                    } else {
                        UI_UpdateScreen("SAI THE MASTER!", "Huy thao tac quan ly", "");
                        Buzzer_Beep(3, 80);
                        HAL_Delay(1200);
                        Door_Lock();
                    }
                } else if (sys_state == STATE_RFID_ADMIN_SCAN_CARD) {
                    RFID_ToggleCard(card_uid);
                } else if (sys_state == STATE_NORMAL) {
                    if (RFID_IsAuthorized(card_uid)) {
                        Door_Unlock("Phuong thuc: The RFID", 1);
                    } else {
                        Handle_AuthFailure("THE KHONG HOP LE!");
                    }
                }
                HAL_Delay(500); // Tránh đọc lặp cùng 1 lần quẹt thẻ
            }
        }

        // 5. Quét bàn phím Keypad 4x4 (UC2, UC4, UC6)
        char key = Keypad_GetKey();
        if (key != 0) {
            Buzzer_Beep(1, 30);

            if (key == '*') {
                // Xóa bộ đệm nhập hoặc thoát về màn hình chính
                pin_idx = 0;
                memset(input_pin, 0, sizeof(input_pin));
                Door_Lock();
            }
            else if (key == 'A' && sys_state == STATE_NORMAL) {
                // UC6: Bật/Tắt chế độ Vắng nhà
                away_mode = !away_mode;
                if (!away_mode) {
                    alarm_active = false;
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
                }
                UI_UpdateScreen(away_mode ? "DA BAT VANG NHA" : "DA TAT VANG NHA",
                                "Cam bien PIR:", away_mode ? "DANG GIAM SAT" : "NGUNG HOAT DONG");
            }
            else if (key == 'B' && sys_state == STATE_NORMAL) {
                // UC4: Bắt đầu đổi mật khẩu
                sys_state = STATE_CHANGE_PASS_OLD;
                pin_idx = 0;
                memset(input_pin, 0, sizeof(input_pin));
                UI_UpdateScreen("DOI MAT KHAU", "Nhap MK cu (4 so):", "Bam # de xac nhan");
            }
            else if (key == 'C' && sys_state == STATE_NORMAL) {
                // UC5: Quản lý thẻ RFID
                sys_state = STATE_RFID_ADMIN_WAIT_MASTER;
                UI_UpdateScreen("QUAN LY THE RFID", "Vui long quet THE", "MASTER de tiep tuc");
            }
            else if (key >= '0' && key <= '9') {
                if (pin_idx < PASS_LEN) {
                    input_pin[pin_idx++] = key;
                    input_pin[pin_idx] = '\0';

                    char mask_str[16] = "PIN: ";
                    for (uint8_t i = 0; i < pin_idx; i++) strcat(mask_str, "*");
                    UI_UpdateScreen("DANG NHAP MAT KHAU", mask_str, "Bam # OK | * Xoa");
                }
            }
            else if (key == '#') {
                if (sys_state == STATE_NORMAL) {
                    if (strcmp(input_pin, current_pin) == 0) {
                        Door_Unlock("Phuong thuc: Mat khau", 2);
                    } else {
                        Handle_AuthFailure("SAI MAT KHAU!");
                    }
                }
                else if (sys_state == STATE_CHANGE_PASS_OLD) {
                    if (strcmp(input_pin, current_pin) == 0) {
                        sys_state = STATE_CHANGE_PASS_NEW;
                        pin_idx = 0;
                        memset(input_pin, 0, sizeof(input_pin));
                        UI_UpdateScreen("MK CU CHINH XAC", "Nhap MK moi (4 so):", "Bam # de luu");
                    } else {
                        UI_UpdateScreen("SAI MAT KHAU CU!", "Huy doi mat khau", "");
                        Buzzer_Beep(3, 80);
                        HAL_Delay(1200);
                        Door_Lock();
                    }
                }
                else if (sys_state == STATE_CHANGE_PASS_NEW) {
                    if (pin_idx == PASS_LEN) {
                        strcpy(current_pin, input_pin);
                        Buzzer_Beep(2, 100);
                        UI_UpdateScreen("THANH CONG!", "Da cap nhat mat khau", "moi cho he thong");
                        HAL_Delay(1500);
                        Door_Lock();
                    } else {
                        UI_UpdateScreen("LOI DO DAI", "Mat khau phai du", "4 chu so!");
                    }
                }
            }
        }
    }
}
/* ================= CÁC HÀM KHỞI TẠO PHẦN CỨNG ================= */

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

static void MX_SPI1_Init(void) {
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;
    HAL_SPI_Init(&hspi1);
}

static void MX_TIM2_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 71;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 19999;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim2);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 500;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_USART1_UART_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    // PA4: RC522_CS
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA1, PA2, PA3: Keypad Cột 1-3
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PB0 (RC522_RST), PB8 (LED Đỏ), PB9 (LED Xanh), PB11 (Buzzer), PB12-15 (Keypad Hàng)
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11 |
                          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB1: Keypad Cột 4
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB10: Cảm biến PIR
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PC13: LED tích hợp trên board Blue Pill
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}
