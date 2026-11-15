#ifndef INC_RC522_H_
#define INC_RC522_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define RC522_OK       0
#define RC522_NOTAGERR 1
#define RC522_ERR      2

void RC522_Init(SPI_HandleTypeDef *hspi);
uint8_t RC522_Check(uint8_t *id); // Đọc UID 4 byte của thẻ

#endif /* INC_RC522_H_ */
