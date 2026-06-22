#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "FreeRTOS.h"
#include "main.h"

BaseType_t app_tasks_create(UART_HandleTypeDef *cli_uart,
                            I2C_HandleTypeDef *lcd_i2c);

#endif /* APP_TASKS_H */
