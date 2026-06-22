#ifndef LCD16X2_I2C_H_
#define LCD16X2_I2C_H_

#include <stdbool.h>

#include "main.h"

bool lcd16x2_i2c_init(I2C_HandleTypeDef *i2c_handle);
void lcd16x2_i2c_setCursor(uint8_t row, uint8_t column);
void lcd16x2_i2c_1stLine(void);
void lcd16x2_i2c_2ndLine(void);
void lcd16x2_i2c_TwoLines(void);
void lcd16x2_i2c_OneLine(void);
void lcd16x2_i2c_cursorShow(bool state);
void lcd16x2_i2c_clear(void);
void lcd16x2_i2c_display(bool state);
void lcd16x2_i2c_shiftRight(uint8_t offset);
void lcd16x2_i2c_shiftLeft(uint8_t offset);
void lcd16x2_i2c_printf(const char *format, ...);
void lcd16x2_i2c_write_line(uint8_t row, const char *text);

#endif /* LCD16X2_I2C_H_ */
