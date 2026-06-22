#ifndef CLI_SERVICE_H
#define CLI_SERVICE_H

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

BaseType_t cli_service_init(UART_HandleTypeDef *uart);
void cli_service_bind_task(TaskHandle_t cli_task);
void cli_service_task_step(TickType_t wait_time);
void cli_service_write(const char *text);

#endif /* CLI_SERVICE_H */
