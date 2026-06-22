#include "app_main.h"

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "diagnostics.h"
#include "task.h"

static void app_enter_safe_error_loop(void) {
  taskDISABLE_INTERRUPTS();
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET);
  for (;;) {
  }
}

void app_main_start(UART_HandleTypeDef *cli_uart,
                    I2C_HandleTypeDef *lcd_i2c) {
  if (app_tasks_create(cli_uart, lcd_i2c) != pdPASS) {
    diagnostics_record_fatal(DIAG_FATAL_MALLOC_FAILED);
    app_enter_safe_error_loop();
  }

  vTaskStartScheduler();

  /* 正常情況不應返回。 */
  diagnostics_record_fatal(DIAG_FATAL_MALLOC_FAILED);
  app_enter_safe_error_loop();
}

void app_assert_failed(const char *file, int line) {
  (void)file;
  (void)line;
  diagnostics_record_fatal(DIAG_FATAL_ASSERT);
  app_enter_safe_error_loop();
}

void vApplicationStackOverflowHook(TaskHandle_t task,
                                   char *task_name) {
  (void)task;
  (void)task_name;
  diagnostics_record_fatal(DIAG_FATAL_STACK_OVERFLOW);
  app_enter_safe_error_loop();
}

void vApplicationMallocFailedHook(void) {
  diagnostics_record_fatal(DIAG_FATAL_MALLOC_FAILED);
  app_enter_safe_error_loop();
}
