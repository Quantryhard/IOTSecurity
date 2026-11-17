#ifndef INC_DOOR_HW_H_
#define INC_DOOR_HW_H_

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

void DoorHW_Init(TIM_HandleTypeDef *htim_servo, I2C_HandleTypeDef *hi2c_oled);
void Servo_SetAngle(uint8_t angle);
void Buzzer_Beep(uint8_t times, uint16_t duration_ms);
void LED_SetGreen(bool state);
void LED_SetRed(bool state);
void LED_BlinkGreen(uint8_t times, uint16_t duration_ms);
void LED_BlinkRed(uint8_t times, uint16_t duration_ms);
void UI_UpdateScreen(bool away_mode, const char *line1, const char *line2, const char *line3);

#endif /* INC_DOOR_HW_H_ */
