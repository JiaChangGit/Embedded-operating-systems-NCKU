#include "waveplayer.h"

#include "audio_service.h"
#include "control_service.h"

static AUDIO_ErrorTypeDef waveplayer_send_command(SystemCommandType type,
                                                  int32_t value) {
  SystemCommand command = {.type = type, .value = value};
  return (control_service_send(&command, 0U) == pdPASS) ? AUDIO_ERROR_NONE
                                                        : AUDIO_ERROR_IO;
}

AUDIO_ErrorTypeDef AUDIO_PLAYER_Init(void) { return AUDIO_ERROR_NONE; }

AUDIO_ErrorTypeDef AUDIO_PLAYER_Start(uint8_t index) {
  return waveplayer_send_command(CMD_TRACK_SELECT, index);
}

AUDIO_ErrorTypeDef AUDIO_PLAYER_Process(bool loop) {
  AudioStatusSnapshot status;
  (void)loop;
  audio_service_get_status(&status);
  return (status.state == AUDIO_STATE_ERROR) ? AUDIO_ERROR_IO
                                             : AUDIO_ERROR_NONE;
}

AUDIO_ErrorTypeDef AUDIO_PLAYER_Stop(void) {
  return waveplayer_send_command(CMD_STOP, 0);
}

uint32_t get_uwVolume(void) {
  AudioStatusSnapshot status;
  audio_service_get_status(&status);
  return status.volume;
}

void AUDIO_OUT_TransferComplete_CallBack(void) {
  audio_service_dma_complete_from_isr();
}

void AUDIO_OUT_HalfTransfer_CallBack(void) {
  audio_service_dma_half_from_isr();
}

void AUDIO_OUT_Error_CallBack(void) {
  audio_service_dma_error_from_isr();
}
