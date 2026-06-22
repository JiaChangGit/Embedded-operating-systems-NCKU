#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "control_service.h"
#include "task.h"

#define AUDIO_DMA_BUFFER_SIZE  4096U
#define AUDIO_HALF_BUFFER_SIZE (AUDIO_DMA_BUFFER_SIZE / 2U)

typedef enum {
  AUDIO_STATE_IDLE = 0,
  AUDIO_STATE_USB_WAIT,
  AUDIO_STATE_FILE_READY,
  AUDIO_STATE_PLAYING,
  AUDIO_STATE_PAUSED,
  AUDIO_STATE_STOPPED,
  AUDIO_STATE_ERROR
} AudioState;

typedef struct {
  AudioState state;
  uint16_t current_song_index;
  uint16_t song_count;
  uint8_t volume;
  uint32_t sample_rate;
  char current_song_name[64];
  bool usb_mounted;
  bool file_prepared;
  bool dma_active;
} AudioStatusSnapshot;

typedef struct {
  uint32_t dma_buffer_size;
  uint32_t half_buffer_size;
  uint32_t half_generation[2];
  uint32_t ready_generation[2];
  uint32_t file_queue_used;
  uint32_t file_queue_capacity;
} AudioBufferSnapshot;

BaseType_t audio_service_init(void);
void audio_service_bind_tasks(TaskHandle_t audio_task,
                              TaskHandle_t file_task);
BaseType_t audio_service_post_command(const SystemCommand *command,
                                      TickType_t timeout);
void audio_service_audio_task_step(TickType_t wait_time);
void audio_service_file_task_step(void);

void audio_service_dma_half_from_isr(void);
void audio_service_dma_complete_from_isr(void);
void audio_service_dma_error_from_isr(void);

void audio_service_get_status(AudioStatusSnapshot *snapshot);
void audio_service_get_buffer_snapshot(AudioBufferSnapshot *snapshot);
const char *audio_service_state_name(AudioState state);
UBaseType_t audio_service_file_queue_depth(void);
UBaseType_t audio_service_file_queue_capacity(void);

#endif /* AUDIO_SERVICE_H */
