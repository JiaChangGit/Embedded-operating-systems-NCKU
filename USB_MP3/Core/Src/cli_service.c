#include "cli_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_service.h"
#include "control_service.h"
#include "diagnostics.h"
#include "queue.h"
#include "system_events.h"

#define CLI_DMA_RX_BUFFER_SIZE 64U
#define CLI_RING_BUFFER_SIZE   512U
#define CLI_LINE_BUFFER_SIZE   128U
#define CLI_MAX_ARGUMENTS      6
#define CLI_BINARY_FRAME_SIZE  32U
#define CLI_BINARY_FRAME_TIMEOUT_MS 250U
#define CLI_TASK_STATUS_CAPACITY 12U

typedef bool (*CliCommandHandler)(int argument_count, char **arguments);

typedef struct {
  const char *name;
  CliCommandHandler handler;
  const char *help;
} CliCommandEntry;

static UART_HandleTypeDef *cli_uart;
static TaskHandle_t cli_task_handle;
static uint8_t cli_dma_rx_buffer[CLI_DMA_RX_BUFFER_SIZE];
static volatile uint16_t cli_ring_head;
static volatile uint16_t cli_ring_tail;
static uint8_t cli_ring_buffer[CLI_RING_BUFFER_SIZE];
static char cli_line_buffer[CLI_LINE_BUFFER_SIZE];
static size_t cli_line_length;
static uint8_t cli_binary_frame[CLI_BINARY_FRAME_SIZE];
static uint8_t cli_binary_count;
static uint8_t cli_binary_expected;
static TickType_t cli_binary_last_byte_tick;
static volatile bool cli_rx_started;
static volatile bool cli_rx_restart_pending;

extern DMA_HandleTypeDef hdma_usart3_rx;

static bool cli_command_help(int argument_count, char **arguments);
static bool cli_command_tasks(int argument_count, char **arguments);
static bool cli_command_heap(int argument_count, char **arguments);
static bool cli_command_stats(int argument_count, char **arguments);
static bool cli_command_audio(int argument_count, char **arguments);
static bool cli_command_volume(int argument_count, char **arguments);
static bool cli_command_watchdog(int argument_count, char **arguments);
static bool cli_command_buffers(int argument_count, char **arguments);
static bool cli_command_errors(int argument_count, char **arguments);
static bool cli_command_reset_stats(int argument_count, char **arguments);

static const CliCommandEntry cli_command_table[] = {
    {"help", cli_command_help, "help"},
    {"tasks", cli_command_tasks, "tasks"},
    {"heap", cli_command_heap, "heap"},
    {"stats", cli_command_stats, "stats"},
    {"audio", cli_command_audio,
     "audio play|pause|stop|next|prev"},
    {"volume", cli_command_volume, "volume up|down|set <0-100>"},
    {"watchdog", cli_command_watchdog, "watchdog"},
    {"buffers", cli_command_buffers, "buffers"},
    {"errors", cli_command_errors, "errors"},
    {"reset_stats", cli_command_reset_stats, "reset_stats"},
};

static void cli_write_bytes(const uint8_t *data, size_t length) {
  size_t offset = 0U;

  if ((cli_uart == NULL) || (data == NULL)) {
    return;
  }

  /*
   * USART3 預設 9600 baud。分段 polling TX 避免單次 timeout 太長；
   * 此函式只由低優先權 CliTask 呼叫，不會阻塞 AudioTask。
   */
  while (offset < length) {
    size_t chunk_length = length - offset;
    if (chunk_length > 96U) {
      chunk_length = 96U;
    }
    if (HAL_UART_Transmit(cli_uart, (uint8_t *)&data[offset],
                          (uint16_t)chunk_length, 250U) != HAL_OK) {
      diagnostics_record_uart_error();
      break;
    }
    offset += chunk_length;
  }
}

void cli_service_write(const char *text) {
  if (text != NULL) {
    cli_write_bytes((const uint8_t *)text, strlen(text));
  }
}

static void cli_write_format(const char *format, ...) {
  char output[384];
  va_list arguments;
  int length;

  va_start(arguments, format);
  length = vsnprintf(output, sizeof(output), format, arguments);
  va_end(arguments);
  if (length > 0) {
    size_t transmit_length =
        ((size_t)length < sizeof(output)) ? (size_t)length : sizeof(output) - 1U;
    cli_write_bytes((const uint8_t *)output, transmit_length);
  }
}

static bool cli_send_system_command(SystemCommandType type, int32_t value) {
  SystemCommand command = {.type = type, .value = value};
  if (control_service_send(&command, pdMS_TO_TICKS(20U)) != pdPASS) {
    cli_service_write("ERROR: command queue full\r\n");
    return false;
  }
  cli_service_write("OK\r\n");
  return true;
}

static int cli_tokenize(char *line, char **arguments, int max_arguments) {
  int argument_count = 0;
  char *cursor = line;

  while ((*cursor != '\0') && (argument_count < max_arguments)) {
    while ((*cursor == ' ') || (*cursor == '\t')) {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }
    arguments[argument_count++] = cursor;
    while ((*cursor != '\0') && (*cursor != ' ') && (*cursor != '\t')) {
      cursor++;
    }
    if (*cursor != '\0') {
      *cursor++ = '\0';
    }
  }
  return argument_count;
}

static void cli_dispatch_line(char *line) {
  char *arguments[CLI_MAX_ARGUMENTS];
  int argument_count = cli_tokenize(line, arguments, CLI_MAX_ARGUMENTS);

  if (argument_count == 0) {
    return;
  }

  for (size_t index = 0U;
       index < (sizeof(cli_command_table) / sizeof(cli_command_table[0]));
       ++index) {
    if (strcmp(arguments[0], cli_command_table[index].name) == 0) {
      (void)cli_command_table[index].handler(argument_count, arguments);
      return;
    }
  }
  cli_write_format("ERROR: unknown command '%s'\r\n", arguments[0]);
}

static bool cli_command_help(int argument_count, char **arguments) {
  (void)argument_count;
  (void)arguments;
  cli_service_write("Commands:\r\n");
  for (size_t index = 0U;
       index < (sizeof(cli_command_table) / sizeof(cli_command_table[0]));
       ++index) {
    cli_write_format("  %s\r\n", cli_command_table[index].help);
  }
  return true;
}

static bool cli_command_tasks(int argument_count, char **arguments) {
  static TaskStatus_t task_status[CLI_TASK_STATUS_CAPACITY];
  UBaseType_t task_count;
  (void)argument_count;
  (void)arguments;

  task_count =
      uxTaskGetSystemState(task_status, CLI_TASK_STATUS_CAPACITY, NULL);
  if (task_count == 0U) {
    cli_service_write("ERROR: task table capacity exceeded\r\n");
    return false;
  }

  cli_service_write("Task          State Prio Stack Num\r\n");
  cli_service_write("----------------------------------\r\n");
  for (UBaseType_t index = 0U; index < task_count; ++index) {
    char state_character = '?';

    switch (task_status[index].eCurrentState) {
      case eRunning:
        state_character = 'X';
        break;
      case eReady:
        state_character = 'R';
        break;
      case eBlocked:
        state_character = 'B';
        break;
      case eSuspended:
        state_character = 'S';
        break;
      case eDeleted:
        state_character = 'D';
        break;
      default:
        break;
    }
    cli_write_format("%-13s %c %4lu %5lu %3lu\r\n",
                     task_status[index].pcTaskName, state_character,
                     (unsigned long)task_status[index].uxCurrentPriority,
                     (unsigned long)task_status[index].usStackHighWaterMark,
                     (unsigned long)task_status[index].xTaskNumber);
  }
  return true;
}

static bool cli_command_heap(int argument_count, char **arguments) {
  (void)argument_count;
  (void)arguments;
  cli_write_format("Heap Free: %lu bytes\r\nHeap Minimum: %lu bytes\r\n",
                   (unsigned long)xPortGetFreeHeapSize(),
                   (unsigned long)xPortGetMinimumEverFreeHeapSize());
  return true;
}

static bool cli_command_stats(int argument_count, char **arguments) {
  DiagnosticsSnapshot diagnostics;
  AudioStatusSnapshot audio;
  (void)argument_count;
  (void)arguments;

  diagnostics_get_snapshot(&diagnostics);
  audio_service_get_status(&audio);
  cli_write_format(
      "System Stats:\r\n"
      "Audio State: %s\r\n"
      "Song: %u/%u %s\r\n"
      "Volume: %u\r\n"
      "Heap Free: %lu bytes\r\n"
      "Heap Minimum: %lu bytes\r\n"
      "Audio Underrun: %lu\r\n"
      "File Read Errors: %lu\r\n"
      "UART Rx Errors: %lu\r\n"
      "UART Rx Overflow: %lu\r\n"
      "USB Mount Errors: %lu\r\n",
      audio_service_state_name(audio.state),
      (audio.song_count > 0U) ? (unsigned int)(audio.current_song_index + 1U)
                              : 0U,
      (unsigned int)audio.song_count, audio.current_song_name,
      (unsigned int)audio.volume, (unsigned long)diagnostics.free_heap_bytes,
      (unsigned long)diagnostics.minimum_ever_free_heap_bytes,
      (unsigned long)diagnostics.audio_underrun_count,
      (unsigned long)diagnostics.file_read_error_count,
      (unsigned long)diagnostics.uart_rx_error_count,
      (unsigned long)diagnostics.uart_rx_overflow_count,
      (unsigned long)diagnostics.usb_mount_error_count);
  return true;
}

static bool cli_command_audio(int argument_count, char **arguments) {
  if (argument_count != 2) {
    cli_service_write("ERROR: usage audio play|pause|stop|next|prev\r\n");
    return false;
  }
  if (strcmp(arguments[1], "play") == 0) {
    return cli_send_system_command(CMD_PLAY, 0);
  }
  if (strcmp(arguments[1], "pause") == 0) {
    return cli_send_system_command(CMD_PAUSE, 0);
  }
  if (strcmp(arguments[1], "stop") == 0) {
    return cli_send_system_command(CMD_STOP, 0);
  }
  if (strcmp(arguments[1], "next") == 0) {
    return cli_send_system_command(CMD_NEXT, 0);
  }
  if (strcmp(arguments[1], "prev") == 0) {
    return cli_send_system_command(CMD_PREV, 0);
  }
  cli_service_write("ERROR: invalid audio action\r\n");
  return false;
}

static bool cli_command_volume(int argument_count, char **arguments) {
  char *parse_end;
  long volume;

  if (argument_count == 2) {
    if (strcmp(arguments[1], "up") == 0) {
      return cli_send_system_command(CMD_VOLUME_UP, 0);
    }
    if (strcmp(arguments[1], "down") == 0) {
      return cli_send_system_command(CMD_VOLUME_DOWN, 0);
    }
  } else if ((argument_count == 3) &&
             (strcmp(arguments[1], "set") == 0)) {
    volume = strtol(arguments[2], &parse_end, 10);
    if ((*arguments[2] == '\0') || (*parse_end != '\0') || (volume < 0) ||
        (volume > 100)) {
      cli_service_write("ERROR: volume must be 0-100\r\n");
      return false;
    }
    return cli_send_system_command(CMD_VOLUME_SET, (int32_t)volume);
  }

  cli_service_write("ERROR: usage volume up|down|set <0-100>\r\n");
  return false;
}

static bool cli_command_watchdog(int argument_count, char **arguments) {
  DiagnosticsSnapshot diagnostics;
  (void)argument_count;
  (void)arguments;
  diagnostics_get_snapshot(&diagnostics);
  cli_write_format(
      "Watchdog: %s\r\n"
      "Core Tasks: %s\r\n"
      "Refresh Count: %lu\r\n"
      "Skipped Count: %lu\r\n"
      "Unhealthy Mask: 0x%02lX\r\n",
      diagnostics.watchdog_enabled ? "ENABLED" : "DISABLED",
      (diagnostics.unhealthy_task_mask == 0U) ? "HEALTHY" : "UNHEALTHY",
      (unsigned long)diagnostics.watchdog_refresh_count,
      (unsigned long)diagnostics.watchdog_skipped_count,
      (unsigned long)diagnostics.unhealthy_task_mask);
  return true;
}

static bool cli_command_buffers(int argument_count, char **arguments) {
  AudioBufferSnapshot buffers;
  (void)argument_count;
  (void)arguments;
  audio_service_get_buffer_snapshot(&buffers);
  cli_write_format(
      "Audio DMA Buffer: %lu bytes\r\n"
      "Half Buffer: %lu bytes\r\n"
      "Half0 Generation: %lu/%lu\r\n"
      "Half1 Generation: %lu/%lu\r\n"
      "File Queue: %lu/%lu\r\n"
      "Command Queue: %lu/%lu\r\n",
      (unsigned long)buffers.dma_buffer_size,
      (unsigned long)buffers.half_buffer_size,
      (unsigned long)buffers.ready_generation[0],
      (unsigned long)buffers.half_generation[0],
      (unsigned long)buffers.ready_generation[1],
      (unsigned long)buffers.half_generation[1],
      (unsigned long)buffers.file_queue_used,
      (unsigned long)buffers.file_queue_capacity,
      (unsigned long)control_service_queue_depth(),
      (unsigned long)control_service_queue_capacity());
  return true;
}

static bool cli_command_errors(int argument_count, char **arguments) {
  DiagnosticsSnapshot diagnostics;
  (void)argument_count;
  (void)arguments;
  diagnostics_get_snapshot(&diagnostics);
  cli_write_format(
      "Errors:\r\n"
      "Audio Underrun: %lu\r\n"
      "File Read: %lu\r\n"
      "UART Rx: %lu\r\n"
      "UART Overflow: %lu\r\n"
      "USB Mount: %lu\r\n"
      "Command Queue Full: %lu\r\n"
      "File Queue Full: %lu\r\n"
      "I2S: %lu\r\n"
      "Fatal Reason: %u\r\n",
      (unsigned long)diagnostics.audio_underrun_count,
      (unsigned long)diagnostics.file_read_error_count,
      (unsigned long)diagnostics.uart_rx_error_count,
      (unsigned long)diagnostics.uart_rx_overflow_count,
      (unsigned long)diagnostics.usb_mount_error_count,
      (unsigned long)diagnostics.command_queue_full_count,
      (unsigned long)diagnostics.file_queue_full_count,
      (unsigned long)diagnostics.i2s_error_count,
      (unsigned int)diagnostics.fatal_reason);
  return true;
}

static bool cli_command_reset_stats(int argument_count, char **arguments) {
  (void)argument_count;
  (void)arguments;
  diagnostics_reset_counters();
  cli_service_write("OK: counters reset\r\n");
  return true;
}

static void cli_process_legacy_frame(void) {
  uint8_t checksum = 0U;
  bool checksum_valid;

  for (uint8_t index = 0U; index + 1U < cli_binary_expected; ++index) {
    checksum = (uint8_t)(checksum + cli_binary_frame[index]);
  }
  checksum_valid =
      (cli_binary_frame[cli_binary_expected - 1U] == 0U) ||
      (cli_binary_frame[cli_binary_expected - 1U] == checksum);
  if (!checksum_valid) {
    diagnostics_record_uart_error();
    return;
  }

  for (uint8_t index = 2U; index + 1U < cli_binary_expected; ++index) {
    SystemCommand command = {.value = 0};
    switch (cli_binary_frame[index]) {
      case 0x00:
        command.type = CMD_NEXT;
        break;
      case 0x01:
        command.type = CMD_PREV;
        break;
      case 0x02:
        command.type = CMD_VOLUME_UP;
        break;
      case 0x03:
        command.type = CMD_VOLUME_DOWN;
        break;
      case 0x04:
        command.type = CMD_PAUSE;
        break;
      case 0x05:
        command.type = CMD_PLAY;
        break;
      default:
        diagnostics_record_uart_error();
        continue;
    }
    (void)control_service_send(&command, 0U);
  }
}

static void cli_reset_binary_frame(void) {
  cli_binary_count = 0U;
  cli_binary_expected = 0U;
}

static void cli_process_byte(uint8_t byte) {
  if ((cli_binary_count > 0U) || ((byte == 0xAAU) && (cli_line_length == 0U))) {
    cli_binary_last_byte_tick = xTaskGetTickCount();
    if (cli_binary_count == 0U) {
      cli_binary_frame[0] = byte;
      cli_binary_count = 1U;
      cli_binary_expected = 0U;
      return;
    }

    if (cli_binary_count == 1U) {
      cli_binary_expected = byte;
      cli_binary_frame[cli_binary_count++] = byte;
      if ((cli_binary_expected < 4U) ||
          (cli_binary_expected > CLI_BINARY_FRAME_SIZE)) {
        cli_reset_binary_frame();
        diagnostics_record_uart_error();
      }
      return;
    }

    cli_binary_frame[cli_binary_count++] = byte;
    if (cli_binary_count >= cli_binary_expected) {
      cli_process_legacy_frame();
      cli_reset_binary_frame();
    }
    return;
  }

  if ((byte == '\r') || (byte == '\n')) {
    if (cli_line_length > 0U) {
      cli_line_buffer[cli_line_length] = '\0';
      cli_dispatch_line(cli_line_buffer);
      cli_line_length = 0U;
      cli_service_write("> ");
    }
    return;
  }

  if ((byte == '\b') || (byte == 0x7FU)) {
    if (cli_line_length > 0U) {
      cli_line_length--;
    }
    return;
  }

  if ((byte < 0x20U) || (byte > 0x7EU)) {
    return;
  }

  if (cli_line_length + 1U >= CLI_LINE_BUFFER_SIZE) {
    cli_line_length = 0U;
    diagnostics_record_uart_error();
    cli_service_write("ERROR: input line too long\r\n> ");
    return;
  }
  cli_line_buffer[cli_line_length++] = (char)byte;
}

static bool cli_ring_pop(uint8_t *byte) {
  uint16_t tail;

  if (byte == NULL) {
    return false;
  }
  tail = cli_ring_tail;
  if (tail == cli_ring_head) {
    return false;
  }
  *byte = cli_ring_buffer[tail];
  cli_ring_tail = (uint16_t)((tail + 1U) % CLI_RING_BUFFER_SIZE);
  return true;
}

BaseType_t cli_service_init(UART_HandleTypeDef *uart) {
  if (uart == NULL) {
    return pdFAIL;
  }
  cli_uart = uart;
  cli_ring_head = 0U;
  cli_ring_tail = 0U;
  cli_line_length = 0U;
  cli_reset_binary_frame();
  cli_binary_last_byte_tick = 0U;
  cli_rx_started = false;
  cli_rx_restart_pending = false;
  return pdPASS;
}

void cli_service_bind_task(TaskHandle_t cli_task) {
  cli_task_handle = cli_task;
}

void cli_service_task_step(TickType_t wait_time) {
  uint8_t byte;
  bool restart_receive;

  taskENTER_CRITICAL();
  restart_receive = cli_rx_restart_pending;
  cli_rx_restart_pending = false;
  taskEXIT_CRITICAL();

  if (restart_receive) {
    (void)HAL_UART_AbortReceive(cli_uart);
    cli_rx_started = false;
  }

  if (!cli_rx_started) {
    if (HAL_UARTEx_ReceiveToIdle_DMA(cli_uart, cli_dma_rx_buffer,
                                     sizeof(cli_dma_rx_buffer)) == HAL_OK) {
      __HAL_DMA_DISABLE_IT(&hdma_usart3_rx, DMA_IT_HT);
      cli_rx_started = true;
      cli_service_write(
          "\r\nFreeRTOS USB Audio Player CLI\r\nType 'help' for commands.\r\n> ");
    } else {
      diagnostics_record_uart_error();
      vTaskDelay(pdMS_TO_TICKS(100U));
      return;
    }
  }

  (void)ulTaskNotifyTake(pdTRUE, wait_time);
  while (cli_ring_pop(&byte)) {
    cli_process_byte(byte);
  }

  /*
   * 不完整的舊版 binary frame 不可永久占住 parser；逾時後回到文字 CLI。
   */
  if ((cli_binary_count > 0U) &&
      ((xTaskGetTickCount() - cli_binary_last_byte_tick) >
       pdMS_TO_TICKS(CLI_BINARY_FRAME_TIMEOUT_MS))) {
    cli_reset_binary_frame();
    diagnostics_record_uart_error();
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size) {
  BaseType_t higher_priority_task_woken = pdFALSE;

  if ((uart != cli_uart) || !cli_rx_started) {
    return;
  }

  if (size > CLI_DMA_RX_BUFFER_SIZE) {
    size = CLI_DMA_RX_BUFFER_SIZE;
    diagnostics_record_uart_error_from_isr();
  }

  for (uint16_t index = 0U; index < size; ++index) {
    uint16_t next_head =
        (uint16_t)((cli_ring_head + 1U) % CLI_RING_BUFFER_SIZE);
    if (next_head == cli_ring_tail) {
      diagnostics_record_uart_overflow_from_isr();
      break;
    }
    cli_ring_buffer[cli_ring_head] = cli_dma_rx_buffer[index];
    cli_ring_head = next_head;
  }

  if (HAL_UARTEx_ReceiveToIdle_DMA(cli_uart, cli_dma_rx_buffer,
                                   sizeof(cli_dma_rx_buffer)) != HAL_OK) {
    diagnostics_record_uart_error_from_isr();
    cli_rx_restart_pending = true;
  } else {
    __HAL_DMA_DISABLE_IT(&hdma_usart3_rx, DMA_IT_HT);
  }

  if (cli_task_handle != NULL) {
    vTaskNotifyGiveFromISR(cli_task_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {
  BaseType_t higher_priority_task_woken = pdFALSE;

  if (uart != cli_uart) {
    return;
  }
  diagnostics_record_uart_error_from_isr();
  cli_rx_restart_pending = true;
  if (cli_task_handle != NULL) {
    vTaskNotifyGiveFromISR(cli_task_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}
