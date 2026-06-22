#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

#ifndef ENABLE_IWDG_MONITOR
#define ENABLE_IWDG_MONITOR 0
#endif

typedef enum {
  DIAG_TASK_AUDIO = 0,
  DIAG_TASK_FILE,
  DIAG_TASK_CONTROL,
  DIAG_TASK_UI,
  DIAG_TASK_MONITOR,
  DIAG_TASK_CLI,
  DIAG_TASK_COUNT
} DiagnosticsTaskId;

typedef enum {
  DIAG_FATAL_NONE = 0,
  DIAG_FATAL_STACK_OVERFLOW,
  DIAG_FATAL_MALLOC_FAILED,
  DIAG_FATAL_ASSERT
} DiagnosticsFatalReason;

typedef struct {
  uint32_t uptime_ms;
  uint32_t free_heap_bytes;
  uint32_t minimum_ever_free_heap_bytes;
  uint32_t audio_underrun_count;
  uint32_t file_read_error_count;
  uint32_t uart_rx_error_count;
  uint32_t uart_rx_overflow_count;
  uint32_t usb_mount_error_count;
  uint32_t command_queue_full_count;
  uint32_t file_queue_full_count;
  uint32_t i2s_error_count;
  uint32_t watchdog_refresh_count;
  uint32_t watchdog_skipped_count;
  uint32_t command_queue_used;
  uint32_t command_queue_capacity;
  uint32_t file_queue_used;
  uint32_t file_queue_capacity;
  uint32_t heartbeat_age_ms[DIAG_TASK_COUNT];
  uint32_t stack_high_water_words[DIAG_TASK_COUNT];
  uint32_t unhealthy_task_mask;
  DiagnosticsFatalReason fatal_reason;
  bool watchdog_enabled;
} DiagnosticsSnapshot;

void diagnostics_init(void);
void diagnostics_register_task(DiagnosticsTaskId task_id,
                               TaskHandle_t task_handle);
void diagnostics_heartbeat(DiagnosticsTaskId task_id);
void diagnostics_update_queue_usage(uint32_t command_used,
                                    uint32_t command_capacity,
                                    uint32_t file_used,
                                    uint32_t file_capacity);
void diagnostics_collect(DiagnosticsSnapshot *snapshot);
void diagnostics_get_snapshot(DiagnosticsSnapshot *snapshot);
void diagnostics_reset_counters(void);

void diagnostics_record_audio_underrun(void);
void diagnostics_record_file_error(void);
void diagnostics_record_uart_error(void);
void diagnostics_record_uart_error_from_isr(void);
void diagnostics_record_uart_overflow_from_isr(void);
void diagnostics_record_usb_mount_error(void);
void diagnostics_record_command_queue_full(void);
void diagnostics_record_command_queue_full_from_isr(void);
void diagnostics_record_file_queue_full(void);
void diagnostics_record_i2s_error(void);
void diagnostics_record_i2s_error_from_isr(void);
void diagnostics_record_fatal(DiagnosticsFatalReason reason);

bool diagnostics_core_tasks_healthy(void);
void diagnostics_watchdog_init(void);
bool diagnostics_watchdog_refresh_if_healthy(void);
void diagnostics_print_uart(UART_HandleTypeDef *uart);

#endif /* DIAGNOSTICS_H */
