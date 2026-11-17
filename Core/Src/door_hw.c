#include "door_hw.h"
#include "ssd1306.h"

static TIM_HandleTypeDef *servo_tim;

void DoorHW_Init(TIM_HandleTypeDef *htim_servo, I2C_HandleTypeDef *hi2c_oled) {
    servo_tim = htim_servo;
    HAL_TIM_PWM_Start(servo_tim, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, 1500); // Dừng Servo 360
    LED_SetGreen(false);
    LED_SetRed(false);
    SSD1306_Init(hi2c_oled);
}

// Servo 360: Tăng lên 660ms để đạt đủ 10/10 hành trình mở và đóng
void Servo_SetAngle(uint8_t angle) {
    if (angle == 90) {
        __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, 1200); // Quay mở
        HAL_Delay(660);                                        // Đủ 10/10 góc mở
        __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, 1500); // Dừng lại
    } else if (angle == 0) {
        __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, 1800); // Quay đóng
        HAL_Delay(660);                                        // Khớp thời gian mở
        __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, 1500); // Dừng lại
    } else {
        __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, 1500); // Đứng yên
    }
}

// Đã sửa lại: Đèn Xanh = PC13, Đèn Đỏ = PB8
void LED_SetGreen(bool state) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void LED_SetRed(bool state) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void LED_BlinkGreen(uint8_t times, uint16_t duration_ms) {
    for (uint8_t i = 0; i < times; i++) {
        LED_SetGreen(true);
        HAL_Delay(duration_ms);
        LED_SetGreen(false);
        if (i < times - 1) HAL_Delay(80);
    }
}

void LED_BlinkRed(uint8_t times, uint16_t duration_ms) {
    for (uint8_t i = 0; i < times; i++) {
        LED_SetRed(true);
        HAL_Delay(duration_ms);
        LED_SetRed(false);
        if (i < times - 1) HAL_Delay(80);
    }
}

void Buzzer_Beep(uint8_t times, uint16_t duration_ms) {
    for (uint8_t i = 0; i < times; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
        HAL_Delay(duration_ms);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
        if (i < times - 1) HAL_Delay(80);
    }
}

void UI_UpdateScreen(bool away_mode, const char *line1, const char *line2, const char *line3) {
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
