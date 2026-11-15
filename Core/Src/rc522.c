#include "rc522.h"

#define CS_LOW()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)
#define RST_LOW()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)
#define RST_HIGH() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)

static SPI_HandleTypeDef *rfid_spi;

static void RC522_WriteReg(uint8_t addr, uint8_t val) {
    uint8_t tx[2] = {(addr << 1) & 0x7E, val};
    CS_LOW();
    HAL_SPI_Transmit(rfid_spi, tx, 2, 50);
    CS_HIGH();
}

static uint8_t RC522_ReadReg(uint8_t addr) {
    uint8_t tx[2] = {((addr << 1) & 0x7E) | 0x80, 0x00};
    uint8_t rx[2] = {0};
    CS_LOW();
    HAL_SPI_TransmitReceive(rfid_spi, tx, rx, 2, 50);
    CS_HIGH();
    return rx[1];
}

static void RC522_SetBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) | mask);
}

static void RC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) & (~mask));
}

void RC522_Init(SPI_HandleTypeDef *hspi) {
    rfid_spi = hspi;
    RST_LOW();
    HAL_Delay(10);
    RST_HIGH();
    HAL_Delay(10);

    RC522_WriteReg(0x01, 0x0F); // SoftReset
    HAL_Delay(10);
    RC522_WriteReg(0x2A, 0x8D); // TModeReg
    RC522_WriteReg(0x2B, 0x3E); // TPrescalerReg
    RC522_WriteReg(0x2D, 30);   // TReloadRegL
    RC522_WriteReg(0x2C, 0);    // TReloadRegH
    RC522_WriteReg(0x15, 0x40); // TxASKReg
    RC522_WriteReg(0x11, 0x3D); // ModeReg

    // Bật ăng-ten
    if (!(RC522_ReadReg(0x14) & 0x03)) {
        RC522_SetBitMask(0x14, 0x03);
    }
}

static uint8_t RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen) {
    uint8_t status = RC522_ERR;
    uint8_t irqEn = (command == 0x0C) ? 0x77 : 0x00;
    uint8_t waitIRq = (command == 0x0C) ? 0x30 : 0x00;

    RC522_WriteReg(0x02, irqEn | 0x80);
    RC522_ClearBitMask(0x04, 0x80);
    RC522_SetBitMask(0x0A, 0x80);
    RC522_WriteReg(0x01, 0x00);

    for (uint8_t i = 0; i < sendLen; i++) {
        RC522_WriteReg(0x09, sendData[i]);
    }

    RC522_WriteReg(0x01, command);
    if (command == 0x0C) RC522_SetBitMask(0x0D, 0x80);

    uint16_t i = 2000;
    uint8_t n;
    do {
        n = RC522_ReadReg(0x04);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    RC522_ClearBitMask(0x0D, 0x80);

    if (i != 0) {
        if (!(RC522_ReadReg(0x06) & 0x1B)) {
            status = RC522_OK;
            if (n & irqEn & 0x01) status = RC522_NOTAGERR;
            if (command == 0x0C) {
                n = RC522_ReadReg(0x0A);
                uint8_t lastBits = RC522_ReadReg(0x0C) & 0x07;
                *backLen = lastBits ? ((n - 1) * 8 + lastBits) : (n * 8);
                if (n == 0) n = 1;
                if (n > 16) n = 16;
                for (uint8_t j = 0; j < n; j++) backData[j] = RC522_ReadReg(0x09);
            }
        }
    }
    return status;
}

uint8_t RC522_Check(uint8_t *id) {
    uint8_t status;
    uint16_t backBits;
    uint8_t req = 0x26; // PICC_REQIDL

    RC522_WriteReg(0x0D, 0x07);
    status = RC522_ToCard(0x0C, &req, 1, id, &backBits);
    if ((status != RC522_OK) || (backBits != 0x10)) return RC522_ERR;

    // Anti-collision đọc 4 byte UID
    uint8_t serNum[2] = {0x93, 0x20};
    RC522_WriteReg(0x0D, 0x00);
    status = RC522_ToCard(0x0C, serNum, 2, id, &backBits);
    if (status == RC522_OK) {
        uint8_t serNumCheck = id[0] ^ id[1] ^ id[2] ^ id[3];
        if (serNumCheck != id[4]) status = RC522_ERR;
    }
    return status;
}
