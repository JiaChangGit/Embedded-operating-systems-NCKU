#include "audio_service.h"

#include <stdio.h>
#include <string.h>

#include "AUDIO.h"
#include "File_Handling.h"
#include "diagnostics.h"
#include "fatfs.h"
#include "queue.h"
#include "semphr.h"
#include "system_events.h"
#include "usb_host.h"

#define AUDIO_CONTROL_QUEUE_LENGTH 12U
#define AUDIO_FILE_QUEUE_LENGTH    10U
#define AUDIO_DMA_EVENT_QUEUE_LENGTH 8U

#define AUDIO_NOTIFY_DMA_EVENT     (1UL << 0)
#define AUDIO_NOTIFY_FILE_READY    (1UL << 1)
#define AUDIO_NOTIFY_CONTROL       (1UL << 2)
#define AUDIO_NOTIFY_USB_LOST      (1UL << 3)
#define AUDIO_NOTIFY_ALL           (0x0FUL)

#define AUDIO_DMA_HALFWORD_COUNT \
  (AUDIO_DMA_BUFFER_SIZE / sizeof(uint16_t))
#define AUDIO_DMA_HALFWORD_MARGIN 64U

typedef enum {
  AUDIO_FILE_REQUEST_PREPARE = 0,
  AUDIO_FILE_REQUEST_REFILL,
  AUDIO_FILE_REQUEST_CLOSE
} AudioFileRequestType;

typedef enum {
  AUDIO_DMA_EVENT_HALF = 0,
  AUDIO_DMA_EVENT_COMPLETE,
  AUDIO_DMA_EVENT_ERROR
} AudioDmaEvent;

typedef struct {
  AudioFileRequestType type;
  uint16_t song_index;
  uint8_t half_index;
  uint32_t generation;
  uint32_t session_id;
  bool auto_start;
} AudioFileRequest;

typedef struct {
  AudioState state;
  uint16_t current_song_index;
  uint16_t song_count;
  uint8_t volume;
  uint32_t sample_rate;
  char current_song_name[FILEMGR_FILE_NAME_SIZE];
  bool usb_mounted;
  bool file_prepared;
  bool dma_active;
  bool auto_start_when_ready;
  bool file_open;
  bool eof_pending;
  uint8_t eof_drain_events;
  uint32_t data_remaining;
  uint32_t stream_session;
  volatile uint32_t half_generation[2];
  volatile uint32_t ready_generation[2];
} AudioServiceContext;

typedef union {
  uint32_t alignment;
  uint8_t bytes[AUDIO_DMA_BUFFER_SIZE];
  uint16_t halfwords[AUDIO_DMA_HALFWORD_COUNT];
} AudioDmaBuffer;

static AudioServiceContext audio_context;
static QueueHandle_t audio_control_queue;
static QueueHandle_t audio_file_queue;
static QueueHandle_t audio_dma_event_queue;
static SemaphoreHandle_t audio_state_mutex;
static TaskHandle_t audio_task_handle;
static TaskHandle_t file_task_handle;
static volatile bool audio_dma_event_overflow;
static FIL audio_file;
static AudioDmaBuffer audio_dma_buffer;
static uint8_t file_read_staging[AUDIO_HALF_BUFFER_SIZE]
    __attribute__((aligned(4)));

extern I2S_HandleTypeDef hi2s3;

static uint16_t read_le16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool audio_is_supported_sample_rate(uint32_t sample_rate) {
  static const uint32_t supported_sample_rates[] = {
      8000U, 11025U, 16000U, 22050U,
      32000U, 44100U, 48000U, 96000U,
  };

  for (size_t index = 0U;
       index < (sizeof(supported_sample_rates) /
                sizeof(supported_sample_rates[0]));
       ++index) {
    if (sample_rate == supported_sample_rates[index]) {
      return true;
    }
  }
  return false;
}

static void audio_lock(void) {
  if (audio_state_mutex != NULL) {
    (void)xSemaphoreTake(audio_state_mutex, portMAX_DELAY);
  }
}

static void audio_unlock(void) {
  if (audio_state_mutex != NULL) {
    (void)xSemaphoreGive(audio_state_mutex);
  }
}

/*
 * 只允許 FileTask 在 DMA 正在播放另一半，且距離下一個邊界仍有安全餘量時
 * 寫入目標 half-buffer，避免 f_read() 完成得太晚而與 DMA 同時存取。
 */
static bool audio_dma_half_is_writable(uint8_t half_index) {
  uint32_t remaining_halfwords;
  uint32_t halfword_count = AUDIO_DMA_HALFWORD_COUNT / 2U;

  if (!audio_context.dma_active) {
    return true;
  }
  if ((half_index > 1U) || (hi2s3.hdmatx == NULL)) {
    return false;
  }

  remaining_halfwords = __HAL_DMA_GET_COUNTER(hi2s3.hdmatx);
  if ((remaining_halfwords == 0U) ||
      (remaining_halfwords > AUDIO_DMA_HALFWORD_COUNT)) {
    return false;
  }

  if (half_index == 0U) {
    return (remaining_halfwords <= halfword_count) &&
           (remaining_halfwords > AUDIO_DMA_HALFWORD_MARGIN);
  }

  return (remaining_halfwords > halfword_count) &&
         ((remaining_halfwords - halfword_count) >
          AUDIO_DMA_HALFWORD_MARGIN);
}

static void audio_set_state(AudioState state) {
  audio_lock();
  audio_context.state = state;
  audio_unlock();

  system_events_clear(SYS_EVENT_PLAYING | SYS_EVENT_PAUSED);
  if (state == AUDIO_STATE_PLAYING) {
    system_events_set(SYS_EVENT_PLAYING);
  } else if (state == AUDIO_STATE_PAUSED) {
    system_events_set(SYS_EVENT_PAUSED);
  } else if (state == AUDIO_STATE_ERROR) {
    system_events_set(SYS_EVENT_ERROR);
  }
}

static BaseType_t audio_send_file_request(const AudioFileRequest *request,
                                          TickType_t timeout) {
  if ((request == NULL) || (audio_file_queue == NULL)) {
    return pdFAIL;
  }
  if (xQueueSend(audio_file_queue, request, timeout) != pdPASS) {
    diagnostics_record_file_queue_full();
    return pdFAIL;
  }
  if (file_task_handle != NULL) {
    xTaskNotifyGive(file_task_handle);
  }
  return pdPASS;
}

static void audio_close_file(void) {
  if (audio_context.file_open) {
    (void)f_close(&audio_file);
    audio_context.file_open = false;
  }
}

static FRESULT audio_parse_wav(FIL *file, uint32_t *sample_rate,
                               uint32_t *data_size,
                               uint32_t *data_offset) {
  uint8_t riff_header[12];
  uint8_t chunk_header[8];
  uint8_t format_data[16];
  UINT bytes_read;
  bool format_found = false;
  bool data_found = false;
  uint16_t audio_format = 0U;
  uint16_t channel_count = 0U;
  uint16_t block_align = 0U;
  uint16_t bits_per_sample = 0U;
  uint32_t byte_rate = 0U;

  if ((file == NULL) || (sample_rate == NULL) || (data_size == NULL) ||
      (data_offset == NULL)) {
    return FR_INVALID_PARAMETER;
  }

  if ((f_read(file, riff_header, sizeof(riff_header), &bytes_read) != FR_OK) ||
      (bytes_read != sizeof(riff_header)) ||
      (memcmp(&riff_header[0], "RIFF", 4U) != 0) ||
      (memcmp(&riff_header[8], "WAVE", 4U) != 0)) {
    return FR_INVALID_OBJECT;
  }

  while (f_tell(file) + sizeof(chunk_header) <= f_size(file)) {
    uint32_t chunk_size;
    FSIZE_t current_position;
    FSIZE_t file_size;
    FSIZE_t next_chunk;

    if ((f_read(file, chunk_header, sizeof(chunk_header), &bytes_read) !=
         FR_OK) ||
        (bytes_read != sizeof(chunk_header))) {
      return FR_DISK_ERR;
    }

    chunk_size = read_le32(&chunk_header[4]);
    current_position = f_tell(file);
    file_size = f_size(file);
    if ((current_position > file_size) ||
        (chunk_size > (uint32_t)(file_size - current_position)) ||
        (((chunk_size & 1U) != 0U) &&
         (current_position + chunk_size >= file_size))) {
      return FR_INVALID_OBJECT;
    }
    next_chunk = current_position + chunk_size + (chunk_size & 1U);

    if (memcmp(&chunk_header[0], "fmt ", 4U) == 0) {
      if ((chunk_size < sizeof(format_data)) ||
          (f_read(file, format_data, sizeof(format_data), &bytes_read) !=
           FR_OK) ||
          (bytes_read != sizeof(format_data))) {
        return FR_INVALID_OBJECT;
      }
      audio_format = read_le16(&format_data[0]);
      channel_count = read_le16(&format_data[2]);
      *sample_rate = read_le32(&format_data[4]);
      byte_rate = read_le32(&format_data[8]);
      block_align = read_le16(&format_data[12]);
      bits_per_sample = read_le16(&format_data[14]);
      format_found = true;
    } else if (memcmp(&chunk_header[0], "data", 4U) == 0) {
      *data_offset = (uint32_t)f_tell(file);
      *data_size = chunk_size;
      data_found = true;
    }

    if (format_found && data_found) {
      break;
    }
    if ((next_chunk > f_size(file)) ||
        (f_lseek(file, next_chunk) != FR_OK)) {
      return FR_INVALID_OBJECT;
    }
  }

  /* 現有 codec/I2S 路徑只支援 stereo 16-bit PCM。 */
  if (!format_found || !data_found || (audio_format != 1U) ||
      (channel_count != 2U) || (bits_per_sample != 16U) ||
      (block_align != 4U) || (byte_rate != (*sample_rate * 4U)) ||
      !audio_is_supported_sample_rate(*sample_rate) || (*data_size == 0U) ||
      ((*data_size & 0x3U) != 0U)) {
    return FR_INVALID_OBJECT;
  }

  return f_lseek(file, *data_offset);
}

static bool audio_session_is_current(uint32_t session_id) {
  bool is_current;

  audio_lock();
  is_current = (audio_context.stream_session == session_id);
  audio_unlock();
  return is_current;
}

static void audio_mark_file_error_if_current(uint32_t session_id) {
  bool is_current;

  audio_lock();
  is_current = (audio_context.stream_session == session_id);
  if (is_current) {
    audio_context.state = AUDIO_STATE_ERROR;
    system_events_clear(SYS_EVENT_PLAYING | SYS_EVENT_PAUSED);
    system_events_set(SYS_EVENT_ERROR);
  }
  audio_unlock();
  if (is_current) {
    diagnostics_record_file_error();
  }
}

static bool audio_prepare_file(uint16_t song_index, bool auto_start,
                               uint32_t session_id) {
  const char *song_name;
  char full_path[4U + FILEMGR_FILE_NAME_SIZE + 2U];
  uint32_t sample_rate = 0U;
  uint32_t data_size = 0U;
  uint32_t data_offset = 0U;
  UINT bytes_read = 0U;
  uint32_t initial_read_size;
  FRESULT result;

  if (!audio_session_is_current(session_id)) {
    return false;
  }

  song_name = AUDIO_GetWavName(song_index);
  if (song_name == NULL) {
    audio_mark_file_error_if_current(session_id);
    return false;
  }

  audio_close_file();
  (void)snprintf(full_path, sizeof(full_path), "%s/%s", USBHPath, song_name);
  result = f_open(&audio_file, full_path, FA_READ);
  if (result != FR_OK) {
    audio_mark_file_error_if_current(session_id);
    return false;
  }
  audio_context.file_open = true;

  if (!audio_session_is_current(session_id)) {
    audio_close_file();
    return false;
  }

  result =
      audio_parse_wav(&audio_file, &sample_rate, &data_size, &data_offset);
  if (result != FR_OK) {
    audio_close_file();
    audio_mark_file_error_if_current(session_id);
    return false;
  }

  initial_read_size =
      (data_size < AUDIO_DMA_BUFFER_SIZE) ? data_size : AUDIO_DMA_BUFFER_SIZE;
  result = f_read(&audio_file, audio_dma_buffer.bytes, initial_read_size,
                  &bytes_read);
  if ((result != FR_OK) || (bytes_read != initial_read_size)) {
    audio_close_file();
    audio_mark_file_error_if_current(session_id);
    return false;
  }
  if (bytes_read < AUDIO_DMA_BUFFER_SIZE) {
    memset(&audio_dma_buffer.bytes[bytes_read], 0,
           AUDIO_DMA_BUFFER_SIZE - bytes_read);
  }

  audio_lock();
  if (audio_context.stream_session != session_id) {
    audio_unlock();
    audio_close_file();
    return false;
  }
  audio_context.current_song_index = song_index;
  audio_context.sample_rate = sample_rate;
  audio_context.data_remaining = data_size - bytes_read;
  audio_context.file_prepared = true;
  audio_context.auto_start_when_ready = auto_start;
  audio_context.state = AUDIO_STATE_FILE_READY;
  audio_context.half_generation[0] = 1U;
  audio_context.half_generation[1] = 1U;
  audio_context.ready_generation[0] = 1U;
  audio_context.ready_generation[1] = 1U;
  audio_context.eof_pending = (audio_context.data_remaining == 0U);
  audio_context.eof_drain_events =
      audio_context.eof_pending ? 2U : 0U;
  strncpy(audio_context.current_song_name, song_name,
          sizeof(audio_context.current_song_name) - 1U);
  audio_context
      .current_song_name[sizeof(audio_context.current_song_name) - 1U] = '\0';
  system_events_set(SYS_EVENT_FILE_READY);
  audio_unlock();

  if (audio_task_handle != NULL) {
    (void)xTaskNotify(audio_task_handle, AUDIO_NOTIFY_FILE_READY, eSetBits);
  }
  return true;
}

static void audio_refill_half(const AudioFileRequest *request) {
  uint32_t read_size;
  UINT bytes_read = 0U;
  FRESULT result = FR_OK;
  uint8_t *destination;
  bool generation_is_current;
  bool session_is_current;

  if ((request == NULL) || (request->half_index > 1U)) {
    return;
  }

  audio_lock();
  generation_is_current =
      audio_context.file_open && audio_context.file_prepared &&
      (audio_context.stream_session == request->session_id) &&
      (audio_context.half_generation[request->half_index] ==
       request->generation);
  read_size = (audio_context.data_remaining < AUDIO_HALF_BUFFER_SIZE)
                  ? audio_context.data_remaining
                  : AUDIO_HALF_BUFFER_SIZE;
  audio_unlock();
  if (!generation_is_current) {
    return;
  }

  if (read_size > 0U) {
    result = f_read(&audio_file, file_read_staging, read_size, &bytes_read);
  }

  if ((result != FR_OK) || (bytes_read != read_size)) {
    memset(file_read_staging, 0, sizeof(file_read_staging));
    audio_mark_file_error_if_current(request->session_id);
    return;
  }
  if (bytes_read < AUDIO_HALF_BUFFER_SIZE) {
    memset(&file_read_staging[bytes_read], 0,
           AUDIO_HALF_BUFFER_SIZE - bytes_read);
  }

  destination = &audio_dma_buffer.bytes[(uint32_t)request->half_index *
                                        AUDIO_HALF_BUFFER_SIZE];

  audio_lock();
  session_is_current =
      audio_context.file_open && audio_context.file_prepared &&
      (audio_context.stream_session == request->session_id);
  generation_is_current =
      session_is_current &&
      (audio_context.half_generation[request->half_index] ==
       request->generation);

  /*
   * f_read() 成功後檔案指標已前進。即使這次 refill 錯過 DMA deadline，
   * data_remaining 仍必須同步扣除，否則播放到檔尾會誤判成短讀取錯誤。
   */
  if (session_is_current) {
    audio_context.data_remaining -= bytes_read;
    if ((audio_context.data_remaining == 0U) && !audio_context.eof_pending) {
      audio_context.eof_pending = true;
      audio_context.eof_drain_events = 2U;
    }
  }

  if (generation_is_current &&
      audio_dma_half_is_writable(request->half_index)) {
    memcpy(destination, file_read_staging, AUDIO_HALF_BUFFER_SIZE);
    audio_context.ready_generation[request->half_index] = request->generation;
  }
  audio_unlock();
}

static void audio_stop_driver(void) {
  bool was_active;

  audio_lock();
  was_active = audio_context.dma_active;
  audio_context.dma_active = false;
  audio_unlock();

  if (was_active) {
    if (AUDIO_OUT_Stop(CODEC_PDWN_SW) != AUDIO_OK) {
      diagnostics_record_i2s_error();
    }
  }
  if (audio_dma_event_queue != NULL) {
    (void)xQueueReset(audio_dma_event_queue);
  }
  system_events_clear(SYS_EVENT_PLAYING | SYS_EVENT_PAUSED);
}

static bool audio_start_prepared(void) {
  uint32_t sample_rate;
  uint8_t volume;
  bool prepared;

  audio_lock();
  prepared = audio_context.file_prepared;
  sample_rate = audio_context.sample_rate;
  volume = audio_context.volume;
  audio_unlock();

  if (!prepared) {
    return false;
  }
  if (AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, volume, sample_rate) != AUDIO_OK) {
    diagnostics_record_i2s_error();
    audio_set_state(AUDIO_STATE_ERROR);
    return false;
  }
  if (AUDIO_OUT_Play(audio_dma_buffer.halfwords, AUDIO_DMA_BUFFER_SIZE) !=
      AUDIO_OK) {
    (void)AUDIO_OUT_Stop(CODEC_PDWN_SW);
    diagnostics_record_i2s_error();
    audio_set_state(AUDIO_STATE_ERROR);
    return false;
  }

  audio_lock();
  audio_context.dma_active = true;
  audio_context.auto_start_when_ready = false;
  audio_unlock();
  audio_set_state(AUDIO_STATE_PLAYING);
  return true;
}

static void audio_request_track(uint16_t song_index, bool auto_start) {
  AudioFileRequest request = {0};

  audio_stop_driver();
  audio_lock();
  audio_context.stream_session++;
  audio_context.file_prepared = false;
  audio_context.auto_start_when_ready = false;
  audio_context.eof_pending = false;
  audio_context.eof_drain_events = 0U;
  request.type = AUDIO_FILE_REQUEST_PREPARE;
  request.song_index = song_index;
  request.session_id = audio_context.stream_session;
  request.auto_start = auto_start;
  audio_unlock();
  system_events_clear(SYS_EVENT_FILE_READY);
  if (audio_send_file_request(&request, pdMS_TO_TICKS(20U)) != pdPASS) {
    audio_set_state(AUDIO_STATE_ERROR);
  }
}

static void audio_handle_control_command(const SystemCommand *command) {
  uint16_t song_index;
  uint16_t song_count;
  AudioState state;
  bool file_prepared;
  bool usb_mounted;
  bool dma_active;
  uint8_t volume;

  if (command == NULL) {
    return;
  }

  audio_lock();
  song_index = audio_context.current_song_index;
  song_count = audio_context.song_count;
  state = audio_context.state;
  file_prepared = audio_context.file_prepared;
  usb_mounted = audio_context.usb_mounted;
  dma_active = audio_context.dma_active;
  volume = audio_context.volume;
  audio_unlock();

  switch (command->type) {
    case CMD_PLAY:
      if (state == AUDIO_STATE_PAUSED) {
        if (AUDIO_OUT_Resume() == AUDIO_OK) {
          audio_lock();
          audio_context.dma_active = true;
          audio_unlock();
          audio_set_state(AUDIO_STATE_PLAYING);
        } else {
          diagnostics_record_i2s_error();
          audio_stop_driver();
          audio_set_state(AUDIO_STATE_ERROR);
        }
      } else if (file_prepared) {
        (void)audio_start_prepared();
      } else if (usb_mounted && (song_count > 0U)) {
        audio_request_track(song_index, true);
      }
      break;

    case CMD_PAUSE:
      if (state == AUDIO_STATE_PLAYING) {
        if (AUDIO_OUT_Pause() == AUDIO_OK) {
          audio_set_state(AUDIO_STATE_PAUSED);
        } else {
          diagnostics_record_i2s_error();
          audio_stop_driver();
          audio_set_state(AUDIO_STATE_ERROR);
        }
      }
      break;

    case CMD_STOP: {
      AudioFileRequest close_request = {
          .type = AUDIO_FILE_REQUEST_CLOSE,
      };
      audio_stop_driver();
      audio_lock();
      audio_context.stream_session++;
      audio_context.file_prepared = false;
      audio_context.auto_start_when_ready = false;
      audio_context.eof_pending = false;
      audio_context.eof_drain_events = 0U;
      audio_unlock();
      system_events_clear(SYS_EVENT_FILE_READY);
      if (audio_send_file_request(&close_request, pdMS_TO_TICKS(20U)) !=
          pdPASS) {
        audio_set_state(AUDIO_STATE_ERROR);
        break;
      }
      audio_set_state(AUDIO_STATE_STOPPED);
      break;
    }

    case CMD_NEXT:
      if (song_count > 0U) {
        song_index = (uint16_t)((song_index + 1U) % song_count);
        audio_request_track(song_index, true);
      }
      break;

    case CMD_PREV:
      if (song_count > 0U) {
        song_index =
            (song_index == 0U) ? (uint16_t)(song_count - 1U)
                               : (uint16_t)(song_index - 1U);
        audio_request_track(song_index, true);
      }
      break;

    case CMD_VOLUME_SET:
      if ((command->value >= 0) && (command->value <= 100)) {
        audio_lock();
        audio_context.volume = (uint8_t)command->value;
        dma_active = audio_context.dma_active;
        audio_unlock();
        if (dma_active &&
            (AUDIO_OUT_SetVolume((uint8_t)command->value) != AUDIO_OK)) {
          diagnostics_record_i2s_error();
        }
      }
      break;

    case CMD_VOLUME_UP: {
      int32_t new_volume = (int32_t)volume + 10;
      SystemCommand set_volume = {
          .type = CMD_VOLUME_SET,
          .value = (new_volume > 100) ? 100 : new_volume,
      };
      audio_handle_control_command(&set_volume);
      break;
    }

    case CMD_VOLUME_DOWN: {
      int32_t new_volume = (int32_t)volume - 10;
      SystemCommand set_volume = {
          .type = CMD_VOLUME_SET,
          .value = (new_volume < 0) ? 0 : new_volume,
      };
      audio_handle_control_command(&set_volume);
      break;
    }

    case CMD_TRACK_SELECT:
      if (usb_mounted && (song_count > 0U) && (command->value >= 0) &&
          ((uint32_t)command->value < song_count)) {
        audio_request_track((uint16_t)command->value, true);
      }
      break;

    case CMD_PRINT_STATS:
    case CMD_RESET_STATS:
    default:
      break;
  }
}

static void audio_check_entering_half(uint8_t half_index) {
  uint32_t expected_generation;
  uint32_t ready_generation;
  uint8_t *half_buffer;

  audio_lock();
  expected_generation = audio_context.half_generation[half_index];
  ready_generation = audio_context.ready_generation[half_index];
  if (expected_generation == ready_generation) {
    audio_unlock();
    return;
  }

  /*
   * FileTask missed one half-buffer deadline。遞增 generation 使過期 read
   * 無法 commit，並用 silence 取代，避免把 stale data 當成有效音訊。
   */
  diagnostics_record_audio_underrun();
  audio_context.half_generation[half_index]++;
  half_buffer =
      &audio_dma_buffer.bytes[(uint32_t)half_index * AUDIO_HALF_BUFFER_SIZE];
  memset(half_buffer, 0, AUDIO_HALF_BUFFER_SIZE);
  audio_context.ready_generation[half_index] =
      audio_context.half_generation[half_index];
  audio_unlock();
}

static void audio_request_refill(uint8_t half_index) {
  AudioFileRequest request;

  audio_lock();
  audio_context.half_generation[half_index]++;
  request.type = AUDIO_FILE_REQUEST_REFILL;
  request.song_index = audio_context.current_song_index;
  request.half_index = half_index;
  request.generation = audio_context.half_generation[half_index];
  request.session_id = audio_context.stream_session;
  request.auto_start = false;
  audio_unlock();
  if (audio_send_file_request(&request, 0U) != pdPASS) {
    diagnostics_record_audio_underrun();
  }
}

static void audio_handle_dma_boundary(uint8_t entering_half,
                                      uint8_t refill_half) {
  bool advance_track = false;
  bool playback_active;
  uint16_t song_count;

  audio_lock();
  playback_active = audio_context.dma_active &&
                    (audio_context.state == AUDIO_STATE_PLAYING);
  song_count = audio_context.song_count;
  audio_unlock();
  if (!playback_active) {
    return;
  }

  audio_check_entering_half(entering_half);
  audio_request_refill(refill_half);

  audio_lock();
  if (audio_context.eof_pending && (audio_context.eof_drain_events > 0U)) {
    audio_context.eof_drain_events--;
    if (audio_context.eof_drain_events == 0U) {
      audio_context.eof_pending = false;
      advance_track = true;
    }
  }
  audio_unlock();

  if (advance_track && (song_count > 0U)) {
    SystemCommand next_command = {.type = CMD_NEXT, .value = 0};
    audio_handle_control_command(&next_command);
  }
}

BaseType_t audio_service_init(void) {
  memset(&audio_context, 0, sizeof(audio_context));
  memset(&audio_dma_buffer, 0, sizeof(audio_dma_buffer));
  audio_context.state = AUDIO_STATE_USB_WAIT;
  audio_context.volume = 60U;

  audio_state_mutex = xSemaphoreCreateMutex();
  audio_control_queue =
      xQueueCreate(AUDIO_CONTROL_QUEUE_LENGTH, sizeof(SystemCommand));
  audio_file_queue =
      xQueueCreate(AUDIO_FILE_QUEUE_LENGTH, sizeof(AudioFileRequest));
  audio_dma_event_queue =
      xQueueCreate(AUDIO_DMA_EVENT_QUEUE_LENGTH, sizeof(AudioDmaEvent));
  if ((audio_state_mutex == NULL) || (audio_control_queue == NULL) ||
      (audio_file_queue == NULL) || (audio_dma_event_queue == NULL)) {
    return pdFAIL;
  }

  vQueueAddToRegistry(audio_control_queue, "AudioCmd");
  vQueueAddToRegistry(audio_file_queue, "AudioFile");
  vQueueAddToRegistry(audio_dma_event_queue, "AudioDma");
  audio_dma_event_overflow = false;
  return pdPASS;
}

void audio_service_bind_tasks(TaskHandle_t audio_task,
                              TaskHandle_t file_task) {
  audio_task_handle = audio_task;
  file_task_handle = file_task;
}

BaseType_t audio_service_post_command(const SystemCommand *command,
                                      TickType_t timeout) {
  if ((command == NULL) || (audio_control_queue == NULL)) {
    return pdFAIL;
  }
  if (xQueueSend(audio_control_queue, command, timeout) != pdPASS) {
    diagnostics_record_command_queue_full();
    return pdFAIL;
  }
  if (audio_task_handle != NULL) {
    (void)xTaskNotify(audio_task_handle, AUDIO_NOTIFY_CONTROL, eSetBits);
  }
  return pdPASS;
}

void audio_service_audio_task_step(TickType_t wait_time) {
  uint32_t notifications = 0U;
  SystemCommand command;
  AudioDmaEvent dma_event;
  bool dma_event_overflow;

  (void)xTaskNotifyWait(0U, AUDIO_NOTIFY_ALL, &notifications, wait_time);

  while (xQueueReceive(audio_control_queue, &command, 0U) == pdPASS) {
    audio_handle_control_command(&command);
  }

  if ((notifications & AUDIO_NOTIFY_USB_LOST) != 0U) {
    audio_stop_driver();
    audio_set_state(AUDIO_STATE_USB_WAIT);
  }

  taskENTER_CRITICAL();
  dma_event_overflow = audio_dma_event_overflow;
  audio_dma_event_overflow = false;
  taskEXIT_CRITICAL();
  if (dma_event_overflow) {
    (void)xQueueReset(audio_dma_event_queue);
    audio_stop_driver();
    audio_set_state(AUDIO_STATE_ERROR);
  }
  if ((notifications & AUDIO_NOTIFY_FILE_READY) != 0U) {
    bool auto_start;
    audio_lock();
    auto_start = audio_context.auto_start_when_ready;
    audio_unlock();
    if (auto_start) {
      (void)audio_start_prepared();
    }
  }
  while (xQueueReceive(audio_dma_event_queue, &dma_event, 0U) == pdPASS) {
    switch (dma_event) {
      case AUDIO_DMA_EVENT_HALF:
        /* DMA 正在播放後半段，前半段可交給 FileTask 補資料。 */
        audio_handle_dma_boundary(1U, 0U);
        break;
      case AUDIO_DMA_EVENT_COMPLETE:
        /* DMA 回到前半段，後半段可交給 FileTask 補資料。 */
        audio_handle_dma_boundary(0U, 1U);
        break;
      case AUDIO_DMA_EVENT_ERROR: {
        bool playback_active;
        audio_lock();
        playback_active = audio_context.dma_active;
        audio_unlock();
        if (playback_active) {
          audio_stop_driver();
          audio_set_state(AUDIO_STATE_ERROR);
        }
        break;
      }
      default:
        break;
    }
  }
}

void audio_service_file_task_step(void) {
  static bool previous_connected = false;
  static TickType_t last_mount_attempt = 0U;
  bool connected;
  AudioFileRequest request;
  FRESULT mount_result;
  FRESULT parse_result;

  MX_USB_HOST_Process();
  connected = (Appli_state == APPLICATION_START) ||
              (Appli_state == APPLICATION_READY);

  if (connected) {
    system_events_set(SYS_EVENT_USB_CONNECTED);
  }

  if ((Appli_state == APPLICATION_READY) && !audio_context.usb_mounted &&
      ((xTaskGetTickCount() - last_mount_attempt) >= pdMS_TO_TICKS(500U))) {
    last_mount_attempt = xTaskGetTickCount();
    mount_result = Mount_USB();
    parse_result = (mount_result == FR_OK) ? AUDIO_StorageParse()
                                           : mount_result;
    if ((mount_result == FR_OK) && (parse_result == FR_OK)) {
      uint32_t initial_session;

      audio_lock();
      audio_context.usb_mounted = true;
      audio_context.song_count = AUDIO_GetWavObjectNumber();
      audio_context.current_song_index = 0U;
      audio_context.stream_session++;
      initial_session = audio_context.stream_session;
      audio_unlock();
      system_events_set(SYS_EVENT_USB_MOUNTED);
      if (AUDIO_GetWavObjectNumber() > 0U) {
        (void)audio_prepare_file(0U, true, initial_session);
      } else {
        audio_set_state(AUDIO_STATE_IDLE);
      }
    } else {
      if (mount_result == FR_OK) {
        AUDIO_ClearFileList();
        (void)Unmount_USB();
      }
      diagnostics_record_usb_mount_error();
    }
  }

  if (!connected && previous_connected) {
    audio_close_file();
    if (audio_context.usb_mounted) {
      (void)Unmount_USB();
    }
    (void)xQueueReset(audio_file_queue);
    AUDIO_ClearFileList();
    audio_lock();
    audio_context.stream_session++;
    audio_context.usb_mounted = false;
    audio_context.file_prepared = false;
    audio_context.auto_start_when_ready = false;
    audio_context.song_count = 0U;
    audio_context.current_song_name[0] = '\0';
    audio_unlock();
    system_events_clear(SYS_EVENT_USB_CONNECTED | SYS_EVENT_USB_MOUNTED |
                        SYS_EVENT_FILE_READY | SYS_EVENT_PLAYING |
                        SYS_EVENT_PAUSED);
    if (audio_task_handle != NULL) {
      (void)xTaskNotify(audio_task_handle, AUDIO_NOTIFY_USB_LOST, eSetBits);
    }
  }
  previous_connected = connected;

  /*
   * 每次最多處理四筆，讓 USB Host state machine 仍能穩定被呼叫，
   * 同時可快速清空連續的 DMA refill request。
   */
  for (uint32_t request_count = 0U; request_count < 4U; ++request_count) {
    if (xQueueReceive(audio_file_queue, &request, 0U) != pdPASS) {
      break;
    }
    switch (request.type) {
      case AUDIO_FILE_REQUEST_PREPARE:
        (void)audio_prepare_file(request.song_index, request.auto_start,
                                 request.session_id);
        break;
      case AUDIO_FILE_REQUEST_REFILL:
        audio_refill_half(&request);
        break;
      case AUDIO_FILE_REQUEST_CLOSE:
        audio_close_file();
        break;
      default:
        break;
    }
  }
}

static void audio_send_dma_event_from_isr(AudioDmaEvent event) {
  BaseType_t higher_priority_task_woken = pdFALSE;
  BaseType_t queue_result;

  if ((audio_task_handle == NULL) || (audio_dma_event_queue == NULL)) {
    return;
  }

  queue_result = xQueueSendFromISR(audio_dma_event_queue, &event,
                                  &higher_priority_task_woken);
  if (queue_result != pdPASS) {
    audio_dma_event_overflow = true;
    diagnostics_record_i2s_error_from_isr();
  }
  (void)xTaskNotifyFromISR(audio_task_handle, AUDIO_NOTIFY_DMA_EVENT, eSetBits,
                          &higher_priority_task_woken);
  portYIELD_FROM_ISR(higher_priority_task_woken);
}

void audio_service_dma_half_from_isr(void) {
  audio_send_dma_event_from_isr(AUDIO_DMA_EVENT_HALF);
}

void audio_service_dma_complete_from_isr(void) {
  audio_send_dma_event_from_isr(AUDIO_DMA_EVENT_COMPLETE);
}

void audio_service_dma_error_from_isr(void) {
  diagnostics_record_i2s_error_from_isr();
  audio_send_dma_event_from_isr(AUDIO_DMA_EVENT_ERROR);
}

void audio_service_get_status(AudioStatusSnapshot *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  audio_lock();
  snapshot->state = audio_context.state;
  snapshot->current_song_index = audio_context.current_song_index;
  snapshot->song_count = audio_context.song_count;
  snapshot->volume = audio_context.volume;
  snapshot->sample_rate = audio_context.sample_rate;
  snapshot->usb_mounted = audio_context.usb_mounted;
  snapshot->file_prepared = audio_context.file_prepared;
  snapshot->dma_active = audio_context.dma_active;
  strncpy(snapshot->current_song_name, audio_context.current_song_name,
          sizeof(snapshot->current_song_name) - 1U);
  snapshot->current_song_name[sizeof(snapshot->current_song_name) - 1U] = '\0';
  audio_unlock();
}

void audio_service_get_buffer_snapshot(AudioBufferSnapshot *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  audio_lock();
  snapshot->dma_buffer_size = AUDIO_DMA_BUFFER_SIZE;
  snapshot->half_buffer_size = AUDIO_HALF_BUFFER_SIZE;
  snapshot->half_generation[0] = audio_context.half_generation[0];
  snapshot->half_generation[1] = audio_context.half_generation[1];
  snapshot->ready_generation[0] = audio_context.ready_generation[0];
  snapshot->ready_generation[1] = audio_context.ready_generation[1];
  audio_unlock();
  snapshot->file_queue_used = (uint32_t)audio_service_file_queue_depth();
  snapshot->file_queue_capacity =
      (uint32_t)audio_service_file_queue_capacity();
}

const char *audio_service_state_name(AudioState state) {
  switch (state) {
    case AUDIO_STATE_IDLE:
      return "IDLE";
    case AUDIO_STATE_USB_WAIT:
      return "USB_WAIT";
    case AUDIO_STATE_FILE_READY:
      return "FILE_READY";
    case AUDIO_STATE_PLAYING:
      return "PLAYING";
    case AUDIO_STATE_PAUSED:
      return "PAUSED";
    case AUDIO_STATE_STOPPED:
      return "STOPPED";
    case AUDIO_STATE_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

UBaseType_t audio_service_file_queue_depth(void) {
  return (audio_file_queue != NULL) ? uxQueueMessagesWaiting(audio_file_queue)
                                    : 0U;
}

UBaseType_t audio_service_file_queue_capacity(void) {
  return AUDIO_FILE_QUEUE_LENGTH;
}
