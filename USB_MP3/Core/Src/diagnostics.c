#include "diagnostics.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  volatile uint32_t audio_underrun_count;
  volatile uint32_t file_read_error_count;
  volatile uint32_t uart_rx_error_count;
  volatile uint32_t uart_rx_overflow_count;
  volatile uint32_t usb_mount_error_count;
  volatile uint32_t command_queue_full_count;
  volatile uint32_t file_queue_full_count;
  volatile uint32_t i2s_error_count;
  volatile uint32_t watchdog_refresh_count;
  volatile uint32_t watchdog_skipped_count;
  volatile DiagnosticsFatalReason fatal_reason;
  volatile TickType_t heartbeat_tick[DIAG_TASK_COUNT];
  TaskHandle_t task_handle[DIAG_TASK_COUNT];
  uint32_t stack_high_water_words[DIAG_TASK_COUNT];
  uint32_t command_queue_used;
  uint32_t command_queue_capacity;
  uint32_t file_queue_used;
  uint32_t file_queue_capacity;
  uint32_t unhealthy_task_mask;
  bool watchdog_enabled;
} DiagnosticsContext;

static DiagnosticsContext diagnostics_context;

/* 核心 task 超過此時間未更新 heartbeat，就停止餵 watchdog。 */
static const uint32_t heartbeat_timeout_ms[DIAG_TASK_COUNT] = {
    1000U, 1000U, 1500U, 2000U, 1500U, 2000U};

static uint32_t diagnostics_tick_to_ms(TickType_t ticks) {
  return (uint32_t)(ticks * portTICK_PERIOD_MS);
}

static void diagnostics_increment(volatile uint32_t *counter) {
  taskENTER_CRITICAL();
  (*counter)++;
  taskEXIT_CRITICAL();
}

static void diagnostics_increment_from_isr(volatile uint32_t *counter) {
  UBaseType_t interrupt_mask = taskENTER_CRITICAL_FROM_ISR();
  (*counter)++;
  taskEXIT_CRITICAL_FROM_ISR(interrupt_mask);
}

void diagnostics_init(void) {
  memset(&diagnostics_context, 0, sizeof(diagnostics_context));
}

void diagnostics_register_task(DiagnosticsTaskId task_id,
                               TaskHandle_t task_handle) {
  if (task_id < DIAG_TASK_COUNT) {
    diagnostics_context.task_handle[task_id] = task_handle;
  }
}

void diagnostics_heartbeat(DiagnosticsTaskId task_id) {
  if (task_id < DIAG_TASK_COUNT) {
    diagnostics_context.heartbeat_tick[task_id] = xTaskGetTickCount();
  }
}

void diagnostics_update_queue_usage(uint32_t command_used,
                                    uint32_t command_capacity,
                                    uint32_t file_used,
                                    uint32_t file_capacity) {
  taskENTER_CRITICAL();
  diagnostics_context.command_queue_used = command_used;
  diagnostics_context.command_queue_capacity = command_capacity;
  diagnostics_context.file_queue_used = file_used;
  diagnostics_context.file_queue_capacity = file_capacity;
  taskEXIT_CRITICAL();
}

void diagnostics_collect(DiagnosticsSnapshot *snapshot) {
  TickType_t now;
  uint32_t unhealthy_mask = 0U;

  if (snapshot == NULL) {
    return;
  }

  now = xTaskGetTickCount();
  for (uint32_t index = 0; index < DIAG_TASK_COUNT; ++index) {
    TaskHandle_t task_handle = diagnostics_context.task_handle[index];
    TickType_t heartbeat_tick = diagnostics_context.heartbeat_tick[index];
    uint32_t heartbeat_age_ms =
        diagnostics_tick_to_ms((TickType_t)(now - heartbeat_tick));

    if (task_handle != NULL) {
      diagnostics_context.stack_high_water_words[index] =
          (uint32_t)uxTaskGetStackHighWaterMark(task_handle);
    }

    /*
     * MonitorTask 不檢查自己；系統啟動前三秒也不判定 task stall，
     * 避免 peripheral 初始化較慢時誤觸 watchdog policy。
     */
    if ((index != DIAG_TASK_MONITOR) && (diagnostics_tick_to_ms(now) > 3000U) &&
        ((heartbeat_tick == 0U) ||
         (heartbeat_age_ms > heartbeat_timeout_ms[index]))) {
      unhealthy_mask |= (1UL << index);
    }
  }
  diagnostics_context.unhealthy_task_mask = unhealthy_mask;
  diagnostics_get_snapshot(snapshot);
}

void diagnostics_get_snapshot(DiagnosticsSnapshot *snapshot) {
  TickType_t now;

  if (snapshot == NULL) {
    return;
  }

  now = xTaskGetTickCount();
  taskENTER_CRITICAL();
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->uptime_ms = diagnostics_tick_to_ms(now);
  snapshot->free_heap_bytes = (uint32_t)xPortGetFreeHeapSize();
  snapshot->minimum_ever_free_heap_bytes =
      (uint32_t)xPortGetMinimumEverFreeHeapSize();
  snapshot->audio_underrun_count =
      diagnostics_context.audio_underrun_count;
  snapshot->file_read_error_count =
      diagnostics_context.file_read_error_count;
  snapshot->uart_rx_error_count = diagnostics_context.uart_rx_error_count;
  snapshot->uart_rx_overflow_count =
      diagnostics_context.uart_rx_overflow_count;
  snapshot->usb_mount_error_count =
      diagnostics_context.usb_mount_error_count;
  snapshot->command_queue_full_count =
      diagnostics_context.command_queue_full_count;
  snapshot->file_queue_full_count =
      diagnostics_context.file_queue_full_count;
  snapshot->i2s_error_count = diagnostics_context.i2s_error_count;
  snapshot->watchdog_refresh_count =
      diagnostics_context.watchdog_refresh_count;
  snapshot->watchdog_skipped_count =
      diagnostics_context.watchdog_skipped_count;
  snapshot->command_queue_used = diagnostics_context.command_queue_used;
  snapshot->command_queue_capacity =
      diagnostics_context.command_queue_capacity;
  snapshot->file_queue_used = diagnostics_context.file_queue_used;
  snapshot->file_queue_capacity = diagnostics_context.file_queue_capacity;
  snapshot->unhealthy_task_mask = diagnostics_context.unhealthy_task_mask;
  snapshot->fatal_reason = diagnostics_context.fatal_reason;
  snapshot->watchdog_enabled = diagnostics_context.watchdog_enabled;
  for (uint32_t index = 0; index < DIAG_TASK_COUNT; ++index) {
    snapshot->heartbeat_age_ms[index] = diagnostics_tick_to_ms(
        (TickType_t)(now - diagnostics_context.heartbeat_tick[index]));
    snapshot->stack_high_water_words[index] =
        diagnostics_context.stack_high_water_words[index];
  }
  taskEXIT_CRITICAL();
}

void diagnostics_reset_counters(void) {
  taskENTER_CRITICAL();
  diagnostics_context.audio_underrun_count = 0U;
  diagnostics_context.file_read_error_count = 0U;
  diagnostics_context.uart_rx_error_count = 0U;
  diagnostics_context.uart_rx_overflow_count = 0U;
  diagnostics_context.usb_mount_error_count = 0U;
  diagnostics_context.command_queue_full_count = 0U;
  diagnostics_context.file_queue_full_count = 0U;
  diagnostics_context.i2s_error_count = 0U;
  diagnostics_context.watchdog_refresh_count = 0U;
  diagnostics_context.watchdog_skipped_count = 0U;
  taskEXIT_CRITICAL();
}

void diagnostics_record_audio_underrun(void) {
  diagnostics_increment(&diagnostics_context.audio_underrun_count);
}

void diagnostics_record_file_error(void) {
  diagnostics_increment(&diagnostics_context.file_read_error_count);
}

void diagnostics_record_uart_error(void) {
  diagnostics_increment(&diagnostics_context.uart_rx_error_count);
}

void diagnostics_record_uart_error_from_isr(void) {
  diagnostics_increment_from_isr(&diagnostics_context.uart_rx_error_count);
}

void diagnostics_record_uart_overflow_from_isr(void) {
  diagnostics_increment_from_isr(&diagnostics_context.uart_rx_overflow_count);
}

void diagnostics_record_usb_mount_error(void) {
  diagnostics_increment(&diagnostics_context.usb_mount_error_count);
}

void diagnostics_record_command_queue_full(void) {
  diagnostics_increment(&diagnostics_context.command_queue_full_count);
}

void diagnostics_record_command_queue_full_from_isr(void) {
  diagnostics_increment_from_isr(
      &diagnostics_context.command_queue_full_count);
}

void diagnostics_record_file_queue_full(void) {
  diagnostics_increment(&diagnostics_context.file_queue_full_count);
}

void diagnostics_record_i2s_error(void) {
  diagnostics_increment(&diagnostics_context.i2s_error_count);
}

void diagnostics_record_i2s_error_from_isr(void) {
  diagnostics_increment_from_isr(&diagnostics_context.i2s_error_count);
}

void diagnostics_record_fatal(DiagnosticsFatalReason reason) {
  diagnostics_context.fatal_reason = reason;
}

bool diagnostics_core_tasks_healthy(void) {
  return (diagnostics_context.unhealthy_task_mask == 0U) &&
         (diagnostics_context.fatal_reason == DIAG_FATAL_NONE);
}

void diagnostics_watchdog_init(void) {
#if ENABLE_IWDG_MONITOR
  /*
   * LSI 約 32 kHz，prescaler /64、reload 1249，timeout 約 2.5 秒。
   * IWDG 一旦啟動無法由軟體停止，因此預設 macro 為 0。
   */
  IWDG->KR = 0x5555U;
  IWDG->PR = 4U;
  IWDG->RLR = 1249U;
  while (IWDG->SR != 0U) {
  }
  IWDG->KR = 0xAAAAU;
  IWDG->KR = 0xCCCCU;
  diagnostics_context.watchdog_enabled = true;
#else
  diagnostics_context.watchdog_enabled = false;
#endif
}

bool diagnostics_watchdog_refresh_if_healthy(void) {
  if (!diagnostics_core_tasks_healthy()) {
    diagnostics_increment(&diagnostics_context.watchdog_skipped_count);
    return false;
  }

#if ENABLE_IWDG_MONITOR
  IWDG->KR = 0xAAAAU;
#endif
  if (diagnostics_context.watchdog_enabled) {
    diagnostics_increment(&diagnostics_context.watchdog_refresh_count);
  }
  return true;
}

void diagnostics_print_uart(UART_HandleTypeDef *uart) {
  DiagnosticsSnapshot snapshot;
  char output[384];
  int length;

  if (uart == NULL) {
    return;
  }

  diagnostics_get_snapshot(&snapshot);
  length = snprintf(
      output, sizeof(output),
      "System Stats:\r\n"
      "Heap Free: %lu bytes\r\n"
      "Heap Minimum: %lu bytes\r\n"
      "Audio Underrun: %lu\r\n"
      "File Read Errors: %lu\r\n"
      "UART Rx Errors: %lu\r\n"
      "UART Rx Overflow: %lu\r\n"
      "USB Mount Errors: %lu\r\n"
      "Watchdog Refresh: %lu\r\n",
      (unsigned long)snapshot.free_heap_bytes,
      (unsigned long)snapshot.minimum_ever_free_heap_bytes,
      (unsigned long)snapshot.audio_underrun_count,
      (unsigned long)snapshot.file_read_error_count,
      (unsigned long)snapshot.uart_rx_error_count,
      (unsigned long)snapshot.uart_rx_overflow_count,
      (unsigned long)snapshot.usb_mount_error_count,
      (unsigned long)snapshot.watchdog_refresh_count);
  if (length > 0) {
    size_t transmit_length =
        ((size_t)length < sizeof(output)) ? (size_t)length : sizeof(output) - 1U;
    (void)HAL_UART_Transmit(uart, (uint8_t *)output,
                            (uint16_t)transmit_length, 250U);
  }
}
