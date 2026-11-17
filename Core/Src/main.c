/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body - Smart Door Lock Complete
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "door_hw.h"
#include "esp8266.h"
#include "keypad.h"
#include "rc522.h"
#include "ssd1306.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define PASS_LEN 4
#define MAX_CARDS 5

typedef enum {
    STATE_NORMAL = 0,
    STATE_VERIFY_FOR_PASS,
    STATE_NEW_PASS,
    STATE_VERIFY_FOR_CARD,
    STATE_MANAGE_CARD,
    STATE_VERIFY_MODE_PIN,      // Nhập mã chủ nhà 4 số để bật/tắt Chế độ Vắng nhà
    STATE_VERIFY_OLD_MODE_PIN,  // Nhập mã chủ nhà cũ để đổi mã chủ nhà
    STATE_NEW_MODE_PIN          // Nhập mã chủ nhà mới
} SystemState_t;

SystemState_t sys_state = STATE_NORMAL;
char current_pass[PASS_LEN + 1] = "1234";      // Mật khẩu mở cửa thường
char master_mode_pin[PASS_LEN + 1] = "9999";   // Mã chủ nhà đổi chế độ Vắng nhà

uint8_t valid_cards[MAX_CARDS][4] = {
    {0x12, 0x34, 0x56, 0x78},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
};
uint8_t card_count = 1;

char input_pass[PASS_LEN + 1] = {0};
uint8_t pass_idx = 0;
uint8_t wrong_count = 0;

bool away_mode = false;          // false: Ở nhà | true: Vắng nhà (khóa cứng)
bool is_door_open = false;
bool is_awake = false;
bool door_timeout_alert = false;

uint32_t last_interact_time = 0; // Mốc thời gian cho màn hình nghỉ 5s
uint32_t door_opened_time = 0;   // Mốc thời gian riêng cho cảnh báo quên đóng cửa 15s
uint32_t last_alarm_beep = 0;
uint8_t rx_uid[5];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN 0 */
// Gọi khi có thao tác chủ động (bấm phím, quẹt thẻ, lệnh WiFi) -> Reset cả 5s nghỉ và 15s báo động cửa
void Update_Interaction(void) {
    uint32_t now = HAL_GetTick();
    last_interact_time = now;
    door_opened_time = now;
    door_timeout_alert = false;
}

void Show_Cute_Idle(void) {
    SSD1306_Clear();
    SSD1306_SetCursor(24, 1);
    SSD1306_WriteString("<3  SMART DOOR  <3");
    SSD1306_SetCursor(30, 4);
    SSD1306_WriteString("( ^  v  ^ )");
    SSD1306_SetCursor(28, 6);
    if (away_mode) {
        SSD1306_WriteString("[CHE DO VANG NHA]");
    } else {
        SSD1306_WriteString("Xin chao ban!");
    }
    SSD1306_UpdateScreen();
}

void Show_Current_Prompt(void) {
    char display_buf[16] = {0};
    for (uint8_t i = 0; i < PASS_LEN; i++) {
        if (sys_state == STATE_VERIFY_MODE_PIN || sys_state == STATE_VERIFY_OLD_MODE_PIN || sys_state == STATE_NEW_MODE_PIN) {
            display_buf[i * 2] = (i < pass_idx) ? '*' : '_';
        } else {
            display_buf[i * 2] = (i < pass_idx) ? input_pass[i] : '_';
        }
        display_buf[i * 2 + 1] = ' ';
    }

    if (sys_state == STATE_NORMAL) {
        UI_UpdateScreen(away_mode, "Nhap MK (4 so):", display_buf, "A:Mode B:MK *:MaChu");
    } else if (sys_state == STATE_VERIFY_FOR_PASS) {
        UI_UpdateScreen(away_mode, "DOI MK - Nhap MK cu:", display_buf, "#:Xac nhan  *:Huy");
    } else if (sys_state == STATE_NEW_PASS) {
        UI_UpdateScreen(away_mode, "DOI MK - Nhap MK moi:", display_buf, "#:Luu MK    *:Huy");
    } else if (sys_state == STATE_VERIFY_FOR_CARD) {
        UI_UpdateScreen(away_mode, "THE TU - Nhap MK:", display_buf, "#:Xac nhan  *:Huy");
    } else if (sys_state == STATE_MANAGE_CARD) {
        char info[22];
        snprintf(info, sizeof(info), "Da luu: %d/%d the", card_count, MAX_CARDS);
        UI_UpdateScreen(away_mode, "Quet the de THEM/XOA", info, "Bam [*] de thoat");
    } else if (sys_state == STATE_VERIFY_MODE_PIN) {
        UI_UpdateScreen(away_mode, "NHAP MA CHU NHA:", display_buf, "#:Doi Mode  *:Huy");
    } else if (sys_state == STATE_VERIFY_OLD_MODE_PIN) {
        UI_UpdateScreen(away_mode, "MA CHU NHA CU:", display_buf, "#:Tiep tuc  *:Huy");
    } else if (sys_state == STATE_NEW_MODE_PIN) {
        UI_UpdateScreen(away_mode, "MA CHU NHA MOI:", display_buf, "#:Luu ma    *:Huy");
    }
}

void Reset_Input(void) {
    pass_idx = 0;
    memset(input_pass, 0, sizeof(input_pass));
}

int8_t Find_Card(uint8_t *uid) {
    for (uint8_t i = 0; i < card_count; i++) {
        if (memcmp(valid_cards[i], uid, 4) == 0) {
            return i;
        }
    }
    return -1;
}

void Unlock_Door(const char *reason) {
    Update_Interaction();
    is_awake = true;

    // CHỐT CHẶN TUYỆT ĐỐI KHI Ở CHẾ ĐỘ VẮNG NHÀ
    if (away_mode) {
        LED_BlinkRed(2, 150);
        Buzzer_Beep(3, 150);
        UI_UpdateScreen(away_mode, "CHE DO VANG NHA!", "Cua da khoa cung!", "Tat Mode de mo cua");
        ESP8266_SendLog(0, 1);
        HAL_Delay(1500);
        Show_Current_Prompt();
        return;
    }

    wrong_count = 0;
    if (!is_door_open) {
        UI_UpdateScreen(away_mode, "MO CUA THANH CONG", reason, "Bam [D] de dong cua");
        LED_BlinkGreen(1, 150);
        Buzzer_Beep(1, 120);
        HAL_Delay(100);

        LED_SetRed(false);
        LED_SetGreen(true);

        Servo_SetAngle(90);
        is_door_open = true;
        door_timeout_alert = false;
        door_opened_time = HAL_GetTick();
        ESP8266_SendLog(1, 0);
    } else {
        UI_UpdateScreen(away_mode, "CUA DANG MO SAN", "Bam [D] de dong cua", "");
    }
}

void Lock_Door(void) {
    Update_Interaction();
    if (is_door_open) {
        UI_UpdateScreen(away_mode, "DANG DONG CUA...", "Xin cho mot chut", "");
        LED_SetGreen(false);
        LED_SetRed(false);
        door_timeout_alert = false;

        Buzzer_Beep(2, 80);
        Servo_SetAngle(0);
        is_door_open = false;
        sys_state = STATE_NORMAL;
        Reset_Input();
        Show_Current_Prompt();
    } else {
        UI_UpdateScreen(away_mode, "CUA DA DONG SAN!", "", "");
        HAL_Delay(1000);
        if (is_awake) Show_Current_Prompt();
        else Show_Cute_Idle();
    }
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
    DoorHW_Init(&htim2, &hi2c1);
    RC522_Init(&hspi1);
    UI_UpdateScreen(away_mode, "Khoi dong WiFi...", WIFI_SSID, "Xin cho 2 giay...");
    HAL_Delay(2000); // Chờ ESP32 khởi động xong hoàn toàn trước khi gửi AT

    char esp_ip[20] = {0};
    char debug_msg[25] = {0};

    if (ESP8266_Init(&huart2, esp_ip, debug_msg)) {
        char ip_line[26];
        snprintf(ip_line, sizeof(ip_line), "IP: %s", esp_ip);
        UI_UpdateScreen(away_mode, "WiFi: THANH CONG!", WIFI_SSID, ip_line);
        LED_BlinkGreen(2, 150);
        Buzzer_Beep(2, 100);
        HAL_Delay(3000);
    } else {
        UI_UpdateScreen(away_mode, "WiFi: THAT BAI!", debug_msg, "Xem lai phan cung");
        LED_BlinkRed(2, 200);
        Buzzer_Beep(3, 150);
        HAL_Delay(3000);
    }

    is_awake = false;
    Update_Interaction();
    Show_Cute_Idle();
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    // 1. Cảm biến phát hiện người (PB1) -> Nháy đèn đỏ 1 cái và giữ mốc 10s
        static uint8_t ir_prev_state = 0;
        uint8_t ir_triggered = (HAL_GPIO_ReadPin(Pull_down_GPIO_Port, Pull_down_Pin) == GPIO_PIN_RESET) ? 1 : 0;

        if (ir_triggered) {
            last_interact_time = now; // Gia hạn thêm 10 giây kể từ lúc cảm biến còn quét thấy người

            // Chỉ nháy đèn đỏ 1 cái tại khoảnh khắc vừa phát hiện người (tránh nháy liên tục)
            if (ir_prev_state == 0) {
                LED_BlinkRed(1, 100);
            }

            if (!is_awake) {
                is_awake = true;
                sys_state = STATE_NORMAL;
                Reset_Input();
                Show_Current_Prompt();
            }
        }
        ir_prev_state = ir_triggered;
        // 2. Sau 10 giây không phát hiện người và không có tương tác nào -> Về màn hình mặt cười
            if (is_awake && !is_door_open && (now - last_interact_time >= 10000)) {
                is_awake = false;
                sys_state = STATE_NORMAL;
                Reset_Input();
                LED_BlinkGreen(1, 100);
                Show_Cute_Idle();
            }

    // 3. Cảnh báo khi mở cửa mà không có thao tác tương tác nào quá 15 giây
    if (is_door_open) {
        if (!door_timeout_alert && (now - door_opened_time >= 15000)) {
            door_timeout_alert = true;
            LED_SetGreen(false);
            LED_SetRed(true);
            UI_UpdateScreen(away_mode, "CANH BAO QUEN CUA!", "Mo qua 15 giay!", "Bam [D] de dong cua");
            ESP8266_SendLog(1, 1);
        }
        if (door_timeout_alert && (now - last_alarm_beep >= 600)) {
            last_alarm_beep = now;
            Buzzer_Beep(1, 150);
        }
    }

    // 4. Xử lý các lệnh điều khiển từ xa qua WiFi
    char remote_pass[8] = {0};
    RemoteCmd_t rcmd = ESP8266_GetRemoteCmd(remote_pass);
    if (rcmd != REMOTE_CMD_NONE) {
        Update_Interaction();
        if (rcmd == REMOTE_CMD_OPEN) {
            Unlock_Door("Mo tu xa (WiFi)");
        } else if (rcmd == REMOTE_CMD_LOCK) {
            Lock_Door();
        } else if (rcmd == REMOTE_CMD_TOGGLE_MODE) {
            away_mode = !away_mode;
            if (away_mode && is_door_open) {
                Lock_Door();
            }
            LED_BlinkGreen(1, 120);
            Buzzer_Beep(1, 150);
            if (is_awake) Show_Current_Prompt();
            else Show_Cute_Idle();
        } else if (rcmd == REMOTE_CMD_CHANGE_PASS) {
            memcpy(current_pass, remote_pass, PASS_LEN);
            current_pass[PASS_LEN] = '\0';
            LED_BlinkGreen(2, 120);
            Buzzer_Beep(2, 80);
            UI_UpdateScreen(away_mode, "DOI MK TU XA!", "Da cap nhat MK moi", "");
            HAL_Delay(1500);
            if (is_awake) Show_Current_Prompt();
            else Show_Cute_Idle();
        } else if (rcmd == REMOTE_CMD_CHANGE_MPIN) {
            memcpy(master_mode_pin, remote_pass, PASS_LEN);
            master_mode_pin[PASS_LEN] = '\0';
            LED_BlinkGreen(2, 120);
            Buzzer_Beep(2, 80);
            UI_UpdateScreen(away_mode, "DOI MA CHU NHA!", "Da cap nhat tu xa", "");
            HAL_Delay(1500);
            if (is_awake) Show_Current_Prompt();
            else Show_Cute_Idle();
        }
    }

    // 5. Xử lý Quẹt thẻ & Bàn phím khi màn hình đang thức
    if (is_awake) {
        if (RC522_Check(rx_uid) == RC522_OK) {
            Update_Interaction();
            uint8_t card_4b[4];
            memcpy(card_4b, rx_uid, 4);
            int8_t idx = Find_Card(card_4b);

            if (sys_state == STATE_MANAGE_CARD) {
                if (idx >= 0) {
                    for (uint8_t i = idx; i < card_count - 1; i++) {
                        memcpy(valid_cards[i], valid_cards[i + 1], 4);
                    }
                    card_count--;
                    LED_BlinkRed(2, 120);
                    Buzzer_Beep(2, 100);
                    UI_UpdateScreen(away_mode, "DA XOA THE NAY!", "Khoi danh sach", "");
                } else {
                    if (card_count < MAX_CARDS) {
                        memcpy(valid_cards[card_count], card_4b, 4);
                        card_count++;
                        LED_BlinkGreen(2, 120);
                        Buzzer_Beep(1, 200);
                        UI_UpdateScreen(away_mode, "THEM THE THANH CONG!", "Da luu the moi", "");
                    } else {
                        LED_BlinkRed(2, 150);
                        UI_UpdateScreen(away_mode, "BO NHO DAY!", "Toi da 5 the", "");
                    }
                }
                HAL_Delay(1500);
                Show_Current_Prompt();
            } else {
                if (idx >= 0) {
                    Unlock_Door("The RFID hop le");
                } else {
                    char uid_str[22];
                    snprintf(uid_str, sizeof(uid_str), "UID:%02X %02X %02X %02X",
                             card_4b[0], card_4b[1], card_4b[2], card_4b[3]);
                    LED_BlinkRed(1, 200);
                    Buzzer_Beep(2, 100);
                    UI_UpdateScreen(away_mode, "THE KHONG HOP LE!", uid_str, "Thu lai...");
                    ESP8266_SendLog(0, 1);
                    HAL_Delay(1500);
                    Show_Current_Prompt();
                }
            }
        }

        char key = Keypad_GetKey();
        if (key != 0) {
            Buzzer_Beep(1, 35);
            Update_Interaction();

            if (key == 'D') {
                Lock_Door();
            }
            else if (key == 'A' && sys_state == STATE_NORMAL) {
                sys_state = STATE_VERIFY_MODE_PIN;
                Reset_Input();
                Show_Current_Prompt();
            }
            else if (key == 'B' && sys_state == STATE_NORMAL) {
                sys_state = STATE_VERIFY_FOR_PASS;
                Reset_Input();
                Show_Current_Prompt();
            }
            else if (key == 'C' && sys_state == STATE_NORMAL) {
                sys_state = STATE_VERIFY_FOR_CARD;
                Reset_Input();
                Show_Current_Prompt();
            }
            else if (key == '*') {
                if (pass_idx > 0) {
                    Reset_Input();
                } else if (sys_state == STATE_NORMAL) {
                    sys_state = STATE_VERIFY_OLD_MODE_PIN;
                } else {
                    sys_state = STATE_NORMAL;
                }
                Show_Current_Prompt();
            }
            else if (key == '#') {
                if (pass_idx == 0 && sys_state == STATE_NORMAL) {
                    sys_state = STATE_VERIFY_MODE_PIN;
                    Show_Current_Prompt();
                }
                else if (pass_idx == PASS_LEN) {
                    input_pass[PASS_LEN] = '\0';

                    if (sys_state == STATE_NORMAL) {
                        if (strcmp(input_pass, current_pass) == 0) {
                            Unlock_Door("Mat khau dung");
                        } else {
                            wrong_count++;
                            LED_BlinkRed(1, 250);
                            Buzzer_Beep(2, 120);
                            if (wrong_count >= 3) {
                                LED_SetRed(true);
                                UI_UpdateScreen(away_mode, "CANH BAO!", "Sai qua 3 lan!", "Khoa 10 giay...");
                                ESP8266_SendLog(0, 1);
                                Buzzer_Beep(5, 300);
                                HAL_Delay(10000);
                                LED_SetRed(false);
                                wrong_count = 0;
                                Update_Interaction();
                            } else {
                                UI_UpdateScreen(away_mode, "SAI MAT KHAU!", "Vui long thu lai", "");
                                HAL_Delay(1200);
                            }
                            Show_Current_Prompt();
                        }
                    }
                    else if (sys_state == STATE_VERIFY_MODE_PIN) {
                        if (strcmp(input_pass, master_mode_pin) == 0) {
                            away_mode = !away_mode;
                            if (away_mode && is_door_open) {
                                Lock_Door();
                            }
                            LED_BlinkGreen(2, 120);
                            Buzzer_Beep(2, 100);
                            UI_UpdateScreen(away_mode,
                                            away_mode ? "DA BAT: VANG NHA" : "DA TAT: VANG NHA",
                                            away_mode ? "Khoa cung toan bo!" : "Mo cua binh thuong",
                                            "");
                        } else {
                            LED_BlinkRed(2, 200);
                            Buzzer_Beep(3, 150);
                            UI_UpdateScreen(away_mode, "SAI MA CHU NHA!", "Tu choi doi che do", "");
                        }
                        HAL_Delay(1500);
                        sys_state = STATE_NORMAL;
                        Reset_Input();
                        Show_Current_Prompt();
                    }
                    else if (sys_state == STATE_VERIFY_OLD_MODE_PIN) {
                        if (strcmp(input_pass, master_mode_pin) == 0) {
                            LED_BlinkGreen(1, 150);
                            sys_state = STATE_NEW_MODE_PIN;
                        } else {
                            LED_BlinkRed(2, 200);
                            Buzzer_Beep(2, 120);
                            UI_UpdateScreen(away_mode, "SAI MA CHU NHA CU!", "Huy thao tac", "");
                            HAL_Delay(1200);
                            sys_state = STATE_NORMAL;
                        }
                        Reset_Input();
                        Show_Current_Prompt();
                    }
                    else if (sys_state == STATE_NEW_MODE_PIN) {
                        strcpy(master_mode_pin, input_pass);
                        LED_BlinkGreen(2, 120);
                        Buzzer_Beep(2, 100);
                        UI_UpdateScreen(away_mode, "DOI MA CHU NHA OK!", "Da luu ma moi", "");
                        HAL_Delay(1500);
                        sys_state = STATE_NORMAL;
                        Reset_Input();
                        Show_Current_Prompt();
                    }
                    else if (sys_state == STATE_VERIFY_FOR_PASS) {
                        if (strcmp(input_pass, current_pass) == 0) {
                            LED_BlinkGreen(1, 150);
                            sys_state = STATE_NEW_PASS;
                        } else {
                            LED_BlinkRed(1, 250);
                            Buzzer_Beep(2, 120);
                            UI_UpdateScreen(away_mode, "SAI MK HIEN TAI!", "Huy doi mat khau", "");
                            HAL_Delay(1200);
                            sys_state = STATE_NORMAL;
                        }
                        Reset_Input();
                        Show_Current_Prompt();
                    }
                    else if (sys_state == STATE_NEW_PASS) {
                        strcpy(current_pass, input_pass);
                        LED_BlinkGreen(2, 120);
                        Buzzer_Beep(2, 100);
                        UI_UpdateScreen(away_mode, "DOI MK THANH CONG!", "Da luu mat khau moi", "");
                        HAL_Delay(1500);
                        sys_state = STATE_NORMAL;
                        Reset_Input();
                        Show_Current_Prompt();
                    }
                    else if (sys_state == STATE_VERIFY_FOR_CARD) {
                        if (strcmp(input_pass, current_pass) == 0) {
                            LED_BlinkGreen(1, 150);
                            sys_state = STATE_MANAGE_CARD;
                        } else {
                            LED_BlinkRed(1, 250);
                            Buzzer_Beep(2, 120);
                            UI_UpdateScreen(away_mode, "SAI MAT KHAU!", "Khong duoc truy cap", "");
                            HAL_Delay(1200);
                            sys_state = STATE_NORMAL;
                        }
                        Reset_Input();
                        Show_Current_Prompt();
                    }
                    Reset_Input();
                }
            }
            else if (key >= '0' && key <= '9' && sys_state != STATE_MANAGE_CARD) {
                if (pass_idx < PASS_LEN) {
                    input_pass[pass_idx++] = key;
                    Show_Current_Prompt();
                }
            }
        }
    }
  }
  /* USER CODE END WHILE */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_SPI1_Init(void)
{
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
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_TIM2_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 19999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim2);
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, RC522_RST_Pin|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_15|GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RC522_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RC522_CS_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RC522_RST_Pin|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_15|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = Pull_down_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(Pull_down_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
