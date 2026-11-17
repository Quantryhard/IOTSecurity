#include "main.h"
#include "rc522.h"
#include "keypad.h"
#include "esp8266.h"
#include "door_hw.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart2;

#define MAX_CARDS 5
#define PASS_LEN  4

char current_pin[PASS_LEN + 1] = "1234";
char input_pin[PASS_LEN + 1]   = "";
uint8_t pin_idx = 0;

uint8_t master_uid[4] = {0x12, 0x34, 0x56, 0x78};
uint8_t valid_cards[MAX_CARDS][4] = {
    {0x12, 0x34, 0x56, 0x78},
    {0xAA, 0xBB, 0xCC, 0xDD},
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}
};

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
bool away_mode = false;
bool alarm_active = false;
uint8_t wrong_attempts = 0;

uint32_t unlock_timer = 0;
uint32_t lockout_timer = 0;
uint32_t alarm_blink_timer = 0;
uint32_t rfid_poll_timer = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART2_UART_Init(void);

/* ================= LOGIC XỬ LÝ BẢO MẬT ================= */

void Door_Unlock(const char *method_msg, uint8_t log_code) {
    Servo_SetAngle(90);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // Bật LED xanh (PC13 mức thấp)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);  // Tắt LED đỏ (PB8)

    away_mode = false;
    alarm_active = false;
    wrong_attempts = 0;
    pin_idx = 0;
    memset(input_pin, 0, sizeof(input_pin));

    Buzzer_Beep(1, 120);
    UI_UpdateScreen(away_mode, "MO CUA THANH CONG", method_msg, "Tu dong khoa sau 5s");
    ESP8266_SendLog(log_code, 0);

    sys_state = STATE_UNLOCKED;
    unlock_timer = HAL_GetTick();
}

void Door_Lock(void) {
    Servo_SetAngle(0);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   // Tắt LED xanh (PC13 mức cao)
    sys_state = STATE_NORMAL;
    UI_UpdateScreen(away_mode, "CUA DA KHOA", "Quet the hoac nhap PIN", "A:Vang nha | B:Doi MK");
}

void Handle_AuthFailure(const char *reason) {
    wrong_attempts++;
    pin_idx = 0;
    memset(input_pin, 0, sizeof(input_pin));
    Buzzer_Beep(3, 80);

    if (wrong_attempts >= 3) {
        sys_state = STATE_LOCKED_OUT;
        lockout_timer = HAL_GetTick();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET); // Bật sáng LED đỏ (PB8)
        UI_UpdateScreen(away_mode, "CANH BAO BAO MAT!", "Sai qua 3 lan!", "Khoa ban phim 60s");
        ESP8266_SendLog(0, 2);
    } else {
        char attempt_str[24];
        snprintf(attempt_str, sizeof(attempt_str), "Con %d lan thu!", 3 - wrong_attempts);
        UI_UpdateScreen(away_mode, reason, attempt_str, "Vui long thu lai");
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
        UI_UpdateScreen(away_mode, "LOI THAO TAC", "Khong the xoa the", "Master!");
        Buzzer_Beep(2, 100);
        return;
    }
    for (uint8_t i = 1; i < MAX_CARDS; i++) {
        if (memcmp(uid, valid_cards[i], 4) == 0) {
            memset(valid_cards[i], 0, 4);
            Buzzer_Beep(2, 100);
            UI_UpdateScreen(away_mode, "QUAN LY THE RFID", "Da XOA the khoi", "bo nho he thong!");
            HAL_Delay(1500);
            Door_Lock();
            return;
        }
    }
    for (uint8_t i = 1; i < MAX_CARDS; i++) {
        uint8_t empty[4] = {0, 0, 0, 0};
        if (memcmp(valid_cards[i], empty, 4) == 0) {
            memcpy(valid_cards[i], uid, 4);
            Buzzer_Beep(1, 200);
            UI_UpdateScreen(away_mode, "QUAN LY THE RFID", "Da THEM the moi", "thanh cong!");
            HAL_Delay(1500);
            Door_Lock();
            return;
        }
    }
    UI_UpdateScreen(away_mode, "BO NHO DAY", "Toi da 5 the RFID", "");
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
    MX_USART2_UART_Init();

    DoorHW_Init(&htim2, &hi2c1);
    RC522_Init(&hspi1);

    UI_UpdateScreen(away_mode, "Dang ket noi WiFi...", WIFI_SSID, "Vui long cho...");
    ESP8266_Init(&huart2);

    Door_Lock();

    while (1) {
        uint32_t now = HAL_GetTick();

        if (ESP8266_CheckRemoteUnlock()) {
            Door_Unlock("Mo khoa tu xa IoT", 3);
        }

        if (sys_state == STATE_UNLOCKED) {
            if (now - unlock_timer >= 5000) Door_Lock();
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

        // Cảm biến PIR nối chân PB1
        if (away_mode && HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_SET && !alarm_active) {
            alarm_active = true;
            UI_UpdateScreen(away_mode, "BAO DONG DOT NHAP!", "Phat hien chuyen dong", "Dang gui canh bao...");
            ESP8266_SendLog(0, 1);
        }

        // Nháy LED đỏ (PB8) và còi Buzzer (PB9) khi báo động
        if (alarm_active && (now - alarm_blink_timer >= 200)) {
            alarm_blink_timer = now;
            HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_8);
            HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_9);
        }

        if (now - rfid_poll_timer >= 200) {
            rfid_poll_timer = now;
            uint8_t card_uid[5] = {0};
            if (RC522_Check(card_uid) == RC522_OK) {
                if (sys_state == STATE_RFID_ADMIN_WAIT_MASTER) {
                    if (memcmp(card_uid, master_uid, 4) == 0) {
                        Buzzer_Beep(1, 100);
                        sys_state = STATE_RFID_ADMIN_SCAN_CARD;
                        UI_UpdateScreen(away_mode, "XAC THUC MASTER OK", "Quet the can THEM", "hoac XOA...");
                    } else {
                        UI_UpdateScreen(away_mode, "SAI THE MASTER!", "Huy thao tac quan ly", "");
                        Buzzer_Beep(3, 80);
                        HAL_Delay(1200);
                        Door_Lock();
                    }
                } else if (sys_state == STATE_RFID_ADMIN_SCAN_CARD) {
                    RFID_ToggleCard(card_uid);
                } else if (sys_state == STATE_NORMAL) {
                    if (RFID_IsAuthorized(card_uid)) Door_Unlock("Phuong thuc: The RFID", 1);
                    else Handle_AuthFailure("THE KHONG HOP LE!");
                }
                HAL_Delay(500);
            }
        }

        char key = Keypad_GetKey();
        if (key != 0) {
            Buzzer_Beep(1, 30);

            if (key == '*') {
                pin_idx = 0;
                memset(input_pin, 0, sizeof(input_pin));
                Door_Lock();
            } else if (key == 'A' && sys_state == STATE_NORMAL) {
                away_mode = !away_mode;
                if (!away_mode) {
                    alarm_active = false;
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); // Tắt LED đỏ
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); // Tắt Buzzer
                }
                UI_UpdateScreen(away_mode, away_mode ? "DA BAT VANG NHA" : "DA TAT VANG NHA",
                                "Cam bien PIR:", away_mode ? "DANG GIAM SAT" : "NGUNG HOAT DONG");
            } else if (key == 'B' && sys_state == STATE_NORMAL) {
                sys_state = STATE_CHANGE_PASS_OLD;
                pin_idx = 0;
                memset(input_pin, 0, sizeof(input_pin));
                UI_UpdateScreen(away_mode, "DOI MAT KHAU", "Nhap MK cu (4 so):", "Bam # de xac nhan");
            } else if (key == 'C' && sys_state == STATE_NORMAL) {
                sys_state = STATE_RFID_ADMIN_WAIT_MASTER;
                UI_UpdateScreen(away_mode, "QUAN LY THE RFID", "Vui long quet THE", "MASTER de tiep tuc");
            } else if (key >= '0' && key <= '9' && pin_idx < PASS_LEN) {
                input_pin[pin_idx++] = key;
                input_pin[pin_idx] = '\0';
                char mask_str[16] = "PIN: ";
                for (uint8_t i = 0; i < pin_idx; i++) strcat(mask_str, "*");
                UI_UpdateScreen(away_mode, "DANG NHAP MAT KHAU", mask_str, "Bam # OK | * Xoa");
            } else if (key == '#') {
                if (sys_state == STATE_NORMAL) {
                    if (strcmp(input_pin, current_pin) == 0) Door_Unlock("Phuong thuc: Mat khau", 2);
                    else Handle_AuthFailure("SAI MAT KHAU!");
                } else if (sys_state == STATE_CHANGE_PASS_OLD) {
                    if (strcmp(input_pin, current_pin) == 0) {
                        sys_state = STATE_CHANGE_PASS_NEW;
                        pin_idx = 0;
                        memset(input_pin, 0, sizeof(input_pin));
                        UI_UpdateScreen(away_mode, "MK CU CHINH XAC", "Nhap MK moi (4 so):", "Bam # de luu");
                    } else {
                        UI_UpdateScreen(away_mode, "SAI MAT KHAU CU!", "Huy doi mat khau", "");
                        Buzzer_Beep(3, 80);
                        HAL_Delay(1200);
                        Door_Lock();
                    }
                } else if (sys_state == STATE_CHANGE_PASS_NEW) {
                    if (pin_idx == PASS_LEN) {
                        strcpy(current_pin, input_pin);
                        Buzzer_Beep(2, 100);
                        UI_UpdateScreen(away_mode, "THANH CONG!", "Da cap nhat mat khau", "moi cho he thong");
                        HAL_Delay(1500);
                        Door_Lock();
                    } else {
                        UI_UpdateScreen(away_mode, "LOI DO DAI", "Mat khau phai du", "4 chu so!");
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

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
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
    htim2.Init.Prescaler = 63; // Khớp xung hệ thống 64MHz -> chu kỳ PWM 20ms (50Hz)
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

static void MX_USART2_UART_Init(void) {
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    // PA4: RC522_CS
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA8, PA9, PA10, PA11: Keypad Cột 1-4
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PB0 (RC522_RST), PB8 (LED Đỏ), PB9 (Buzzer), PB12-15 (Keypad Hàng 1-4)
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_8 | GPIO_PIN_9 |
                          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB1: Cảm biến PIR
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PC13: LED Xanh (tích cực mức thấp)
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
