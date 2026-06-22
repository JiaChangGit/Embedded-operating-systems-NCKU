/**
 ******************************************************************************
 * @file    stm32f411e_discovery_audio.c
 * @author  MCD Application Team
 * @brief   This file provides the Audio driver for the STM32F411E-Discovery
 *          board.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2017 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *notice, this list of conditions and the following disclaimer in the
 *documentation and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************
 */

/*==============================================================================
                                             User NOTES
1. How To use this driver:
--------------------------
   - This driver supports STM32F411xx devices on STM32F411E-Discovery Kit:
        a) to play an audio file (all functions names start by
BSP_AUDIO_OUT_xxx) b) to record an audio file through MP45DT02, ST MEMS (all
functions names start by AUDIO_IN_xxx)

a) PLAY A FILE:
==============
   + Call the function BSP_AUDIO_OUT_Init(
                                    OutputDevice: physical output mode
(OUTPUT_DEVICE_SPEAKER, OUTPUT_DEVICE_HEADPHONE, OUTPUT_DEVICE_AUTO or
                                                 OUTPUT_DEVICE_BOTH)
                                    Volume: initial volume to be set (0 is min
(mute), 100 is max (100%) AudioFreq: Audio frequency in Hz (8000, 16000, 22500,
32000 ...) this parameter is relative to the audio file/stream type.
                                   )
      This function configures all the hardware required for the audio
application (codec, I2C, I2S, GPIOs, DMA and interrupt if needed). This function
returns 0 if configuration is OK. If the returned value is different from 0 or
the function is stuck then the communication with the codec (try to un-plug the
power or reset device in this case).
      - OUTPUT_DEVICE_SPEAKER: only speaker will be set as output for the audio
stream.
      - OUTPUT_DEVICE_HEADPHONE: only headphones will be set as output for the
audio stream.
      - OUTPUT_DEVICE_AUTO: Selection of output device is made through external
switch (implemented into the audio jack on the discovery board). When the
Headphone is connected it is used as output. When the headphone is disconnected
from the audio jack, the output is automatically switched to Speaker.
      - OUTPUT_DEVICE_BOTH: both Speaker and Headphone are used as outputs for
the audio stream at the same time.
   + Call the function BSP_AUDIO_OUT_Play(
                                  pBuffer: pointer to the audio data file
address Size: size of the buffer to be sent in Bytes
                                 )
      to start playing (for the first time) from the audio file/stream.
   + Call the function BSP_AUDIO_OUT_Pause() to pause playing
   + Call the function BSP_AUDIO_OUT_Resume() to resume playing.
       Note. After calling BSP_AUDIO_OUT_Pause() function for pause, only
BSP_AUDIO_OUT_Resume() should be called for resume (it is not allowed to call
BSP_AUDIO_OUT_Play() in this case). Note. This function should be called only
when the audio file is played or paused (not stopped).
   + For each mode, you may need to implement the relative callback functions
into your code. The Callback functions are named BSP_AUDIO_OUT_XXXCallBack() and
only their prototypes are declared in the stm32f411e_discovery_audio.h file.
(refer to the example for more details on the callbacks implementations)
   + To Stop playing, to modify the volume level, the frequency or to mute, use
the functions BSP_AUDIO_OUT_Stop(), BSP_AUDIO_OUT_SetVolume(),
AUDIO_OUT_SetFrequency() BSP_AUDIO_OUT_SetOutputMode and
BSP_AUDIO_OUT_SetMute().
   + The driver API and the callback functions are at the end of the
stm32f411e_discovery_audio.h file.

Driver architecture:
--------------------
   + This driver provide the High Audio Layer: consists of the function API
exported in the stm32f411e_discovery_audio.h file (BSP_AUDIO_OUT_Init(),
BSP_AUDIO_OUT_Play() ...)
   + This driver provide also the Media Access Layer (MAL): which consists of
functions allowing to access the media containing/ providing the audio
file/stream. These functions are also included as local functions into the
stm32f411e_discovery_audio.c file (I2S3_Init()...)

Known Limitations:
-------------------
   1- When using the Speaker, if the audio file quality is not high enough, the
speaker output may produce high and uncomfortable noise level. To avoid this
issue, to use speaker output properly, try to increase audio file sampling rate
(typically higher than 48KHz). This operation will lead to larger file size. 2-
Communication with the audio codec (through I2C) may be corrupted if it is
interrupted by some user interrupt routines (in this case, interrupts could be
disabled just before the start of communication then re-enabled when it is
over). Note that this communication is only done at the configuration phase
(BSP_AUDIO_OUT_Init() or BSP_AUDIO_OUT_Stop()) and when Volume control
modification is performed (BSP_AUDIO_OUT_SetVolume() or
BSP_AUDIO_OUT_SetMute()or BSP_AUDIO_OUT_SetOutputMode()). When the audio data is
played, no communication is required with the audio codec. 3- Parsing of audio
file is not implemented (in order to determine audio file properties:
Mono/Stereo, Data size, File size, Audio Frequency, Audio Data header size ...).
The configuration is fixed for the given audio file. 4- Supports only Stereo
audio streaming. To play mono audio streams, each data should be sent twice on
the I2S or should be duplicated on the source buffer. Or convert the stream in
stereo before playing. 5- Supports only 16-bits audio data size.

b) RECORD A FILE:
================
   + Call the function BSP_AUDIO_IN_Init(
                                    AudioFreq: Audio frequency in Hz (8000,
16000, 22500, 32000 ...)
                                    )
      This function configures all the hardware required for the audio
application (I2S, GPIOs, DMA and interrupt if needed). This function returns 0
if configuration is OK.

   + Call the function BSP_AUDIO_IN_Record(
                            pbuf Main buffer pointer for the recorded data
storing size Current size of the recorded buffer
                            )
      to start recording from the microphone.

   + User needs to implement user callbacks to retrieve data saved in the record
buffer (AUDIO_IN_RxHalfCpltCallback/BSP_AUDIO_IN_ReceiveComplete_CallBack)

   + Call the function AUDIO_IN_STOP() to stop recording

==============================================================================*/

/**
  ***************************************************************************************************************
  ***************************************************************************************************************
  ***************************************************************************************************************

  File:		  AUDIO.c
  Modifier:   ControllersTech.com
  Updated:    10th JAN 2021

  ***************************************************************************************************************
  Copyright (C) 2017 ControllersTech.com

  This is a free software under the GNU license, you can redistribute it and/or
  modify it under the terms of the GNU General Public License version 3 as
  published by the Free Software Foundation. This software library is shared
  with public for educational purposes, without WARRANTY and Author is not
  liable for any damages caused directly or indirectly by this software, read
  more about this on the GNU General Public License.

  ***************************************************************************************************************
*/

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>

#include "AUDIO.h"
#include "audio_service.h"
#include "FreeRTOS.h"
#include "task.h"

/* These PLL parameters are valid when the f(VCO clock) = 1Mhz */
const uint32_t I2SFreq[8] = {8000,  11025, 16000, 22050,
                             32000, 44100, 48000, 96000};
const uint32_t I2SPLLN[8] = {256, 429, 213, 429, 426, 271, 258, 344};
const uint32_t I2SPLLR[8] = {5, 4, 4, 4, 4, 6, 3, 1};

/*##### PLAY #####*/
static AUDIO_DrvTypeDef *pAudioDrv;
/*
 * 使用 CubeMX 已建立的 I2S3/DMA handle，避免原專案同時維護
 * hi2s3(DMA1_Stream5) 與 hAudioOutI2s(DMA1_Stream7)。
 */
extern I2S_HandleTypeDef hi2s3;
#define hAudioOutI2s hi2s3

static uint8_t I2S3_Init(uint32_t AudioFreq);
extern AUDIO_DrvTypeDef cs43l22_drv;

static int32_t audio_driver_frequency_index(uint32_t audio_frequency) {
  for (size_t index = 0U; index < (sizeof(I2SFreq) / sizeof(I2SFreq[0]));
       ++index) {
    if (I2SFreq[index] == audio_frequency) {
      return (int32_t)index;
    }
  }
  return -1;
}

static bool audio_driver_frequency_supported(uint32_t audio_frequency) {
  return (audio_driver_frequency_index(audio_frequency) >= 0);
}

static HAL_StatusTypeDef audio_driver_configure_clock(uint32_t audio_frequency) {
  RCC_PeriphCLKInitTypeDef clock_configuration;
  int32_t frequency_index =
      audio_driver_frequency_index(audio_frequency);

  if (frequency_index < 0) {
    return HAL_ERROR;
  }

  /*
   * HSE / PLLM = 1 MHz。每個支援的 sample rate 都使用對應的
   * PLLI2SN / PLLI2SR，不能只用 48 kHz 的預設組合。
   */
  HAL_RCCEx_GetPeriphCLKConfig(&clock_configuration);
  clock_configuration.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  clock_configuration.PLLI2S.PLLI2SN = I2SPLLN[frequency_index];
  clock_configuration.PLLI2S.PLLI2SR = I2SPLLR[frequency_index];
  return HAL_RCCEx_PeriphCLKConfig(&clock_configuration);
}

static void audio_driver_delay_ms(uint32_t delay_ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  } else {
    HAL_Delay(delay_ms);
  }
}

/**
 * @brief  Configures the audio peripherals.
 * @param  OutputDevice: OUTPUT_DEVICE_SPEAKER, OUTPUT_DEVICE_HEADPHONE,
 *                       OUTPUT_DEVICE_BOTH or OUTPUT_DEVICE_AUTO .
 * @param  Volume: Initial volume level (from 0 (Mute) to 100 (Max))
 * @param  AudioFreq: Audio frequency used to play the audio stream.
 * @retval AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_Init(uint16_t OutputDevice, uint8_t Volume,
                       uint32_t AudioFreq) {
  uint8_t ret = AUDIO_OK;

  pAudioDrv = NULL;
  if (!audio_driver_frequency_supported(AudioFreq)) {
    return AUDIO_ERROR;
  }

  if (audio_driver_configure_clock(AudioFreq) != HAL_OK) {
    return AUDIO_ERROR;
  }

  /*
   * 使用 CubeMX 的 HAL_I2S_MspInit/hi2s3/hdma_spi3_tx。
   * 若 handle 為 RESET，後續 HAL_I2S_Init 會自行呼叫 MSP init。
   */
  hAudioOutI2s.Instance = I2S3;

  /* I2S data transfer preparation:
  Prepare the Media to be used for the audio transfer from memory to I2S
  peripheral */
  /* Configure the I2S peripheral */
  if (I2S3_Init(AudioFreq) != AUDIO_OK) {
    ret = AUDIO_ERROR;
  }

  if (ret == AUDIO_OK) {
    /* Retieve audio codec identifier */
    if (((cs43l22_drv.ReadID(AUDIO_I2C_ADDRESS)) & CS43L22_ID_MASK) ==
        CS43L22_ID) {
      /* Initialize the audio driver structure */
      pAudioDrv = &cs43l22_drv;
    } else {
      ret = AUDIO_ERROR;
    }
  }

  if (ret == AUDIO_OK) {
    if ((pAudioDrv == NULL) ||
        (pAudioDrv->Init(AUDIO_I2C_ADDRESS, OutputDevice, Volume,
                         AudioFreq) != 0U)) {
      pAudioDrv = NULL;
      ret = AUDIO_ERROR;
    }
  }

  return ret;
}

/**
 * @brief  Starts playing audio stream from a data buffer for a determined size.
 * @param  pBuffer: Pointer to the buffer
 * @param  Size: Number of audio data BYTES.
 * @retval AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_Play(uint16_t *pBuffer, uint32_t Size) {
  uint16_t transfer_count;

  if ((pAudioDrv == NULL) || (pBuffer == NULL) || (Size == 0U) ||
      (Size > UINT16_MAX) || ((Size % AUDIODATA_SIZE) != 0U)) {
    return AUDIO_ERROR;
  }
  transfer_count = (uint16_t)(Size / AUDIODATA_SIZE);

  /* Call the audio Codec Play function */
  if (pAudioDrv->Play(AUDIO_I2C_ADDRESS, pBuffer, (uint16_t)Size) != 0) {
    return AUDIO_ERROR;
  } else {
    /* Update the Media layer and enable it for play */
    if (HAL_I2S_Transmit_DMA(&hAudioOutI2s, pBuffer, transfer_count) !=
        HAL_OK) {
      (void)pAudioDrv->Stop(AUDIO_I2C_ADDRESS, CODEC_PDWN_SW);
      return AUDIO_ERROR;
    }

    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief  Sends n-Bytes on the I2S interface.
 * @param  pData: Pointer to data address
 * @param  Size: Number of data to be written
 */
void AUDIO_OUT_ChangeBuffer(uint16_t *pData, uint16_t Size) {
  if ((pData != NULL) && (Size > 0U)) {
    (void)HAL_I2S_Transmit_DMA(&hAudioOutI2s, pData, Size);
  }
}

/**
 * @brief   Pauses the audio file stream. In case of using DMA, the DMA Pause
 *          feature is used.
 * WARNING: When calling BSP_AUDIO_OUT_Pause() function for pause, only the
 *          BSP_AUDIO_OUT_Resume() function should be called for resume (use of
 * BSP_AUDIO_OUT_Play() function for resume could lead to unexpected behavior).
 * @retval  AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_Pause(void) {
  if (pAudioDrv == NULL) {
    return AUDIO_ERROR;
  }

  /* Call the Audio Codec Pause/Resume function */
  if (pAudioDrv->Pause(AUDIO_I2C_ADDRESS) != 0) {
    return AUDIO_ERROR;
  } else {
    /* Call the Media layer pause function */
    if (HAL_I2S_DMAPause(&hAudioOutI2s) != HAL_OK) {
      return AUDIO_ERROR;
    }

    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief   Resumes the audio file streaming.
 * WARNING: When calling BSP_AUDIO_OUT_Pause() function for pause, only
 *          BSP_AUDIO_OUT_Resume() function should be called for resume (use of
 * BSP_AUDIO_OUT_Play() function for resume could lead to unexpected behavior).
 * @retval  AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_Resume(void) {
  if (pAudioDrv == NULL) {
    return AUDIO_ERROR;
  }

  /* Call the Audio Codec Pause/Resume function */
  if (pAudioDrv->Resume(AUDIO_I2C_ADDRESS) != 0) {
    return AUDIO_ERROR;
  } else {
    /* Call the Media layer resume function */
    if (HAL_I2S_DMAResume(&hAudioOutI2s) != HAL_OK) {
      return AUDIO_ERROR;
    }

    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief  Stops audio playing and Power down the Audio Codec.
 * @param  Option: could be one of the following parameters
 *           - CODEC_PDWN_HW: completely shut down the codec (physically).
 *                            Then need to reconfigure the Codec after power on.
 * @retval AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_Stop(uint32_t Option) {
  HAL_StatusTypeDef dma_status;

  if (pAudioDrv == NULL) {
    return AUDIO_ERROR;
  }

  /* Call DMA Stop to disable DMA stream before stopping codec */
  dma_status = HAL_I2S_DMAStop(&hAudioOutI2s);

  /* Call Audio Codec Stop function */
  if ((pAudioDrv->Stop(AUDIO_I2C_ADDRESS, Option) != 0) ||
      ((dma_status != HAL_OK) && (dma_status != HAL_BUSY))) {
    return AUDIO_ERROR;
  } else {
    if (Option == CODEC_PDWN_HW) {
      /* Wait at least 1ms */
      audio_driver_delay_ms(1U);

      /* Reset the pin */
      HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_RESET);
    }

    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief  Controls the current audio volume level.
 * @param  Volume: Volume level to be set in percentage from 0% to 100% (0 for
 *         Mute and 100 for Max volume level).
 * @retval AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_SetVolume(uint8_t Volume) {
  if (pAudioDrv == NULL) {
    return AUDIO_ERROR;
  }

  /* Call the codec volume control function with converted volume value */
  if (pAudioDrv->SetVolume(AUDIO_I2C_ADDRESS, Volume) != 0) {
    return AUDIO_ERROR;
  } else {
    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief  Enables or disables the MUTE mode by software
 * @param  Cmd: could be AUDIO_MUTE_ON to mute sound or AUDIO_MUTE_OFF to
 *         unmute the codec and restore previous volume level.
 * @retval AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_SetMute(uint32_t Cmd) {
  if (pAudioDrv == NULL) {
    return AUDIO_ERROR;
  }

  /* Call the Codec Mute function */
  if (pAudioDrv->SetMute(AUDIO_I2C_ADDRESS, Cmd) != 0) {
    return AUDIO_ERROR;
  } else {
    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief  Switch dynamically (while audio file is played) the output target
 *         (speaker or headphone).
 * @note   This function modifies a global variable of the audio codec driver:
 * OutputDev.
 * @param  Output: specifies the audio output target: OUTPUT_DEVICE_SPEAKER,
 *         OUTPUT_DEVICE_HEADPHONE, OUTPUT_DEVICE_BOTH or OUTPUT_DEVICE_AUTO
 * @retval AUDIO_OK if correct communication, else wrong communication
 */
uint8_t AUDIO_OUT_SetOutputMode(uint8_t Output) {
  if (pAudioDrv == NULL) {
    return AUDIO_ERROR;
  }

  /* Call the Codec output Device function */
  if (pAudioDrv->SetOutputMode(AUDIO_I2C_ADDRESS, Output) != 0) {
    return AUDIO_ERROR;
  } else {
    /* Return AUDIO_OK when all operations are correctly done */
    return AUDIO_OK;
  }
}

/**
 * @brief  Update the audio frequency.
 * @param  AudioFreq: Audio frequency used to play the audio stream.
 * @note   This API should be called after the BSP_AUDIO_OUT_Init() to adjust
 * the audio frequency.
 */
void AUDIO_OUT_SetFrequency(uint32_t AudioFreq) {
  if (!audio_driver_frequency_supported(AudioFreq)) {
    return;
  }

  if (audio_driver_configure_clock(AudioFreq) != HAL_OK) {
    return;
  }
  /* Update the I2S audio frequency configuration */
  (void)I2S3_Init(AudioFreq);
}

/**
 * @brief  Tx Transfer completed callbacks.
 * @param  hi2s: I2S handle
 */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == I2S3) {
    audio_service_dma_complete_from_isr();
  }
}

/**
 * @brief  Tx Half Transfer completed callbacks.
 * @param  hi2s: I2S handle
 */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == I2S3) {
    audio_service_dma_half_from_isr();
  }
}

/**
 * @brief  Clock Config.
 * @param  hi2s: might be required to set audio peripheral predivider if any.
 * @param  AudioFreq: Audio frequency used to play the audio stream.
 * @note   This API is called by BSP_AUDIO_OUT_Init() and
 * BSP_AUDIO_OUT_SetFrequency() Being __weak it can be overwritten by the
 * application
 * @param  Params : pointer on additional configuration parameters, can be NULL.
 */
__weak void AUDIO_OUT_ClockConfig(I2S_HandleTypeDef *hi2s, uint32_t AudioFreq,
                                  void *Params) {
  (void)hi2s;
  (void)Params;
  (void)audio_driver_configure_clock(AudioFreq);
}

/**
 * @brief  AUDIO OUT I2S MSP Init.
 * @param  hi2s: might be required to set audio peripheral predivider if any.
 * @param  Params : pointer on additional configuration parameters, can be NULL.
 */
__weak void AUDIO_OUT_MspInit(I2S_HandleTypeDef *hi2s, void *Params) {
  (void)Params;
  HAL_I2S_MspInit(hi2s);
  HAL_NVIC_SetPriority(I2S3_DMAx_IRQ, AUDIO_OUT_IRQ_PREPRIO, 0);
  HAL_NVIC_EnableIRQ(I2S3_DMAx_IRQ);
}

/**
 * @brief  De-Initializes BSP_AUDIO_OUT MSP.
 * @param  hi2s: might be required to set audio peripheral predivider if any.
 * @param  Params : pointer on additional configuration parameters, can be NULL.
 */
__weak void AUDIO_OUT_MspDeInit(I2S_HandleTypeDef *hi2s, void *Params) {
  (void)Params;
  HAL_NVIC_DisableIRQ(I2S3_DMAx_IRQ);
  HAL_I2S_MspDeInit(hi2s);
}

/**
 * @brief  Manages the DMA full Transfer complete event.
 */
__weak void AUDIO_OUT_TransferComplete_CallBack(void) {}

/**
 * @brief  Manages the DMA Half Transfer complete event.
 */
__weak void AUDIO_OUT_HalfTransfer_CallBack(void) {}

/**
 * @brief  Manages the DMA FIFO error event.
 */
__weak void AUDIO_OUT_Error_CallBack(void) {}

/*******************************************************************************
                            Static Functions
*******************************************************************************/

/**
 * @brief  Initializes the Audio Codec audio interface (I2S).
 * @param  AudioFreq: Audio frequency to be configured for the I2S peripheral.
 */
static uint8_t I2S3_Init(uint32_t AudioFreq) {
  /* Initialize the hAudioOutI2s Instance parameter */
  hAudioOutI2s.Instance = I2S3;

  /* Disable I2S block */
  __HAL_I2S_DISABLE(&hAudioOutI2s);

  /* I2S3 peripheral configuration */
  hAudioOutI2s.Init.AudioFreq = AudioFreq;
  hAudioOutI2s.Init.ClockSource = I2S_CLOCK_PLL;
  hAudioOutI2s.Init.CPOL = I2S_CPOL_LOW;
  hAudioOutI2s.Init.DataFormat = I2S_DATAFORMAT_16B;
  hAudioOutI2s.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hAudioOutI2s.Init.Mode = I2S_MODE_MASTER_TX;
  hAudioOutI2s.Init.Standard = I2S_STANDARD;

  /* Initialize the I2S peripheral with the structure above */
  if (HAL_I2S_Init(&hAudioOutI2s) != HAL_OK) {
    return AUDIO_ERROR;
  } else {
    return AUDIO_OK;
  }
}
/**
 * @brief  I2S error callbacks.
 * @param  hi2s: I2S handle
 */
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == I2S3) {
    audio_service_dma_error_from_isr();
  }
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
