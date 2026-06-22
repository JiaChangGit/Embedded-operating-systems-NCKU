#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "main.h"

void app_main_start(UART_HandleTypeDef *cli_uart,
                    I2C_HandleTypeDef *lcd_i2c);
void app_assert_failed(const char *file, int line);

#endif /* APP_MAIN_H */
