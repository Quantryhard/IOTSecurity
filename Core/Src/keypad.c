#include "keypad.h"

char Keypad_GetKey(void) {
    const char keymap[4][4] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'}
    };
    const uint16_t row_pins[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};

    for (uint8_t r = 0; r < 4; r++) {
        for (uint8_t i = 0; i < 4; i++) {
            HAL_GPIO_WritePin(GPIOB, row_pins[i], (i == r) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        HAL_Delay(2);

        char pressed = 0;
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8) == GPIO_PIN_RESET) pressed = keymap[r][0];
        else if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_RESET) pressed = keymap[r][1];
        else if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_RESET) pressed = keymap[r][2];
        else if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_RESET) pressed = keymap[r][3];

        if (pressed != 0) {
            HAL_Delay(20);
            while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8) == GPIO_PIN_RESET ||
                   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_RESET ||
                   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_RESET ||
                   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_RESET);
            return pressed;
        }
    }
    return 0;
}
