#include "app_tasks.h"

#include <stdio.h>
#include <string.h>

#include "audio_service.h"
#include "cli_service.h"
#include "control_service.h"
#include "diagnostics.h"
#include "lcd16x2_i2c.h"
#include "system_events.h"
#include "task.h"

#define AUDIO_TASK_STACK_WORDS   512U
#define FILE_TASK_STACK_WORDS    768U
#define CONTROL_TASK_STACK_WORDS 384U
#define UI_TASK_STACK_WORDS      384U
#define MONITOR_TASK_STACK_WORDS 512U
#define CLI_TASK_STACK_WORDS     768U

#define AUDIO_TASK_PRIORITY  (tskIDLE_PRIORITY + 5U)
#define FILE_TASK_PRIORITY   (tskIDLE_PRIORITY + 4U)
#define CONTROL_TASK_PRIORITY (tskIDLE_PRIORITY + 3U)
#define CLI_TASK_PRIORITY     (tskIDLE_PRIORITY + 3U)
#define MONITOR_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#define UI_TASK_PRIORITY      (tskIDLE_PRIORITY + 1U)

static TaskHandle_t audio_task_handle;
static TaskHandle_t file_task_handle;
static TaskHandle_t control_task_handle;
static TaskHandle_t ui_task_handle;
static TaskHandle_t monitor_task_handle;
static TaskHandle_t cli_task_handle;
static I2C_HandleTypeDef *ui_i2c_handle;

static void AudioTask(void *argument);
static void FileTask(void *argument);
static void ControlTask(void *argument);
static void UiTask(void *argument);
static void MonitorTask(void *argument);
static void CliTask(void *argument);

static void AudioTask(void *argument) {
  (void)argument;
  for (;;) {
    diagnostics_heartbeat(DIAG_TASK_AUDIO);
    audio_service_audio_task_step(pdMS_TO_TICKS(100U));
  }
}

static void FileTask(void *argument) {
  (void)argument;
  for (;;) {
    audio_service_file_task_step();
    diagnostics_heartbeat(DIAG_TASK_FILE);
    /*
     * 一般情況每 1 ms service USB Host；收到 refill notify 時可提早醒來。
     */
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1U));
  }
}

static void ControlTask(void *argument) {
  SystemCommand command;
  (void)argument;

  for (;;) {
    diagnostics_heartbeat(DIAG_TASK_CONTROL);
    if (control_service_receive(&command, pdMS_TO_TICKS(100U)) == pdPASS) {
      (void)audio_service_post_command(&command, pdMS_TO_TICKS(20U));
    }
  }
}

static void UiTask(void *argument) {
  AudioStatusSnapshot audio;
  bool lcd_available;
  char first_line[17];
  char second_line[17];
  char previous_first_line[17] = "";
  char previous_second_line[17] = "";
  TickType_t last_wake_time;
  (void)argument;

  diagnostics_heartbeat(DIAG_TASK_UI);
  lcd_available = lcd16x2_i2c_init(ui_i2c_handle);
  if (lcd_available) {
    lcd16x2_i2c_clear();
    lcd16x2_i2c_write_line(0U, "RTOS USB AUDIO");
    lcd16x2_i2c_write_line(1U, "Waiting for USB");
  }

  last_wake_time = xTaskGetTickCount();
  for (;;) {
    diagnostics_heartbeat(DIAG_TASK_UI);
    audio_service_get_status(&audio);

    (void)snprintf(first_line, sizeof(first_line), "%-10s V%3u",
                   audio_service_state_name(audio.state),
                   (unsigned int)audio.volume);
    if (audio.state == AUDIO_STATE_ERROR) {
      (void)snprintf(second_line, sizeof(second_line), "Check CLI errors");
    } else if (audio.current_song_name[0] != '\0') {
      (void)snprintf(second_line, sizeof(second_line), "%.16s",
                     audio.current_song_name);
    } else if (!audio.usb_mounted) {
      (void)snprintf(second_line, sizeof(second_line), "Insert USB drive");
    } else {
      (void)snprintf(second_line, sizeof(second_line), "No WAV files");
    }

    if (lcd_available) {
      if (strncmp(first_line, previous_first_line, 16U) != 0) {
        lcd16x2_i2c_write_line(0U, first_line);
        memcpy(previous_first_line, first_line, sizeof(previous_first_line));
      }
      if (strncmp(second_line, previous_second_line, 16U) != 0) {
        lcd16x2_i2c_write_line(1U, second_line);
        memcpy(previous_second_line, second_line,
               sizeof(previous_second_line));
      }
    }
    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(500U));
  }
}

static void MonitorTask(void *argument) {
  DiagnosticsSnapshot snapshot;
  TickType_t last_wake_time = xTaskGetTickCount();
  (void)argument;

  for (;;) {
    diagnostics_heartbeat(DIAG_TASK_MONITOR);
    diagnostics_update_queue_usage(
        (uint32_t)control_service_queue_depth(),
        (uint32_t)control_service_queue_capacity(),
        (uint32_t)audio_service_file_queue_depth(),
        (uint32_t)audio_service_file_queue_capacity());
    diagnostics_collect(&snapshot);

    if (snapshot.unhealthy_task_mask != 0U) {
      system_events_set(SYS_EVENT_ERROR);
    }
    (void)diagnostics_watchdog_refresh_if_healthy();
    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000U));
  }
}

static void CliTask(void *argument) {
  (void)argument;
  for (;;) {
    diagnostics_heartbeat(DIAG_TASK_CLI);
    cli_service_task_step(pdMS_TO_TICKS(100U));
  }
}

BaseType_t app_tasks_create(UART_HandleTypeDef *cli_uart,
                            I2C_HandleTypeDef *lcd_i2c) {
  if ((cli_uart == NULL) || (lcd_i2c == NULL)) {
    return pdFAIL;
  }

  diagnostics_init();
  if ((system_events_init() != pdPASS) ||
      (control_service_init() != pdPASS) ||
      (audio_service_init() != pdPASS) ||
      (cli_service_init(cli_uart) != pdPASS)) {
    return pdFAIL;
  }
  ui_i2c_handle = lcd_i2c;

  if (xTaskCreate(AudioTask, "AudioTask", AUDIO_TASK_STACK_WORDS, NULL,
                  AUDIO_TASK_PRIORITY, &audio_task_handle) != pdPASS) {
    return pdFAIL;
  }
  if (xTaskCreate(FileTask, "FileTask", FILE_TASK_STACK_WORDS, NULL,
                  FILE_TASK_PRIORITY, &file_task_handle) != pdPASS) {
    return pdFAIL;
  }
  if (xTaskCreate(ControlTask, "ControlTask", CONTROL_TASK_STACK_WORDS, NULL,
                  CONTROL_TASK_PRIORITY, &control_task_handle) != pdPASS) {
    return pdFAIL;
  }
  if (xTaskCreate(UiTask, "UiTask", UI_TASK_STACK_WORDS, NULL, UI_TASK_PRIORITY,
                  &ui_task_handle) != pdPASS) {
    return pdFAIL;
  }
  if (xTaskCreate(MonitorTask, "MonitorTask", MONITOR_TASK_STACK_WORDS, NULL,
                  MONITOR_TASK_PRIORITY, &monitor_task_handle) != pdPASS) {
    return pdFAIL;
  }
  if (xTaskCreate(CliTask, "CliTask", CLI_TASK_STACK_WORDS, NULL,
                  CLI_TASK_PRIORITY, &cli_task_handle) != pdPASS) {
    return pdFAIL;
  }

  audio_service_bind_tasks(audio_task_handle, file_task_handle);
  cli_service_bind_task(cli_task_handle);
  diagnostics_register_task(DIAG_TASK_AUDIO, audio_task_handle);
  diagnostics_register_task(DIAG_TASK_FILE, file_task_handle);
  diagnostics_register_task(DIAG_TASK_CONTROL, control_task_handle);
  diagnostics_register_task(DIAG_TASK_UI, ui_task_handle);
  diagnostics_register_task(DIAG_TASK_MONITOR, monitor_task_handle);
  diagnostics_register_task(DIAG_TASK_CLI, cli_task_handle);
  diagnostics_watchdog_init();
  return pdPASS;
}

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin) {
  static TickType_t last_button_tick;
  static bool button_seen;
  BaseType_t higher_priority_task_woken = pdFALSE;
  SystemCommand command = {.type = CMD_NEXT, .value = 0};

  if (gpio_pin == GPIO_PIN_0) {
    TickType_t now = xTaskGetTickCountFromISR();
    if (button_seen &&
        ((now - last_button_tick) < pdMS_TO_TICKS(150U))) {
      return;
    }
    last_button_tick = now;
    button_seen = true;
    (void)control_service_send_from_isr(&command,
                                        &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}
