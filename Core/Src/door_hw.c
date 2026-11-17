#include "door_hw.h"
#include "ssd1306.h"

static TIM_HandleTypeDef *servo_tim;

void DoorHW_Init(TIM_HandleTypeDef *htim_servo, I2C_HandleTypeDef *hi2c_oled) {
    servo_tim = htim_servo;
    HAL_TIM_PWM_Start(servo_tim, TIM_CHANNEL_1);
    Servo_SetAngle(0);
    SSD1306_Init(hi2c_oled);
}

void Servo_SetAngle(uint8_t angle) {
    if (angle > 180) angle = 180;
    uint32_t pulse = 500 + ((uint32_t)angle * 2000) / 180;
    __HAL_TIM_SET_COMPARE(servo_tim, TIM_CHANNEL_1, pulse);
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
