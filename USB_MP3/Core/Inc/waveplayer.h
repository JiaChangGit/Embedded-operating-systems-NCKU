#ifndef WAVEPLAYER_H
#define WAVEPLAYER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Legacy API compatibility layer.
 * 新程式應直接使用 control_service/audio_service。
 */
typedef enum {
  AUDIO_ERROR_NONE = 0,
  AUDIO_ERROR_IO,
  AUDIO_ERROR_EOF,
  AUDIO_ERROR_INVALID_VALUE
} AUDIO_ErrorTypeDef;

AUDIO_ErrorTypeDef AUDIO_PLAYER_Init(void);
AUDIO_ErrorTypeDef AUDIO_PLAYER_Start(uint8_t index);
AUDIO_ErrorTypeDef AUDIO_PLAYER_Process(bool loop);
AUDIO_ErrorTypeDef AUDIO_PLAYER_Stop(void);
uint32_t get_uwVolume(void);

void AUDIO_OUT_TransferComplete_CallBack(void);
void AUDIO_OUT_HalfTransfer_CallBack(void);
void AUDIO_OUT_Error_CallBack(void);

#endif /* WAVEPLAYER_H */
