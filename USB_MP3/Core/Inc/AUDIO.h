/**
 ******************************************************************************
 * @file    stm32f411e_discovery_audio.h
 * @author  MCD Application Team
 * @brief   This file contains the common defines and functions prototypes for
 *          stm32f411e_discovery_audio.c driver.
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

/**
  ***************************************************************************************************************
  ***************************************************************************************************************
  ***************************************************************************************************************

  File:		  AUDIO.h
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __AUDIO_H
#define __AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "AUDIO_LINK.h"
#include "cs43l22.h"
// #include "pdm2pcm_glo.h"

#define CODEC_STANDARD 0x04
#define I2S_STANDARD I2S_STANDARD_PHILIPS

typedef struct {
  uint32_t (*Init)(uint16_t, uint16_t, uint8_t, uint32_t);
  void (*DeInit)(void);
  uint32_t (*ReadID)(uint16_t);
  uint32_t (*Play)(uint16_t, uint16_t *, uint16_t);
  uint32_t (*Pause)(uint16_t);
  uint32_t (*Resume)(uint16_t);
  uint32_t (*Stop)(uint16_t, uint32_t);
  uint32_t (*SetFrequency)(uint16_t, uint32_t);
  uint32_t (*SetVolume)(uint16_t, uint8_t);
  uint32_t (*SetMute)(uint16_t, uint32_t);
  uint32_t (*SetOutputMode)(uint16_t, uint8_t);
  uint32_t (*Reset)(uint16_t);
} AUDIO_DrvTypeDef;

/*------------------------------------------------------------------------------
                          AUDIO OUT CONFIGURATION
------------------------------------------------------------------------------*/

/* I2S peripheral configuration defines */
#define I2S3 SPI3
#define I2S3_CLK_ENABLE() __HAL_RCC_SPI3_CLK_ENABLE()
#define I2S3_CLK_DISABLE() __HAL_RCC_SPI3_CLK_DISABLE()
#define I2S3_SCK_SD_WS_AF GPIO_AF6_SPI3
#define I2S3_SCK_SD_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define I2S3_MCK_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define I2S3_WS_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define I2S3_WS_PIN GPIO_PIN_4
#define I2S3_SCK_PIN GPIO_PIN_10
#define I2S3_SD_PIN GPIO_PIN_12
#define I2S3_MCK_PIN GPIO_PIN_7
#define I2S3_SCK_SD_GPIO_PORT GPIOC
#define I2S3_WS_GPIO_PORT GPIOA
#define I2S3_MCK_GPIO_PORT GPIOC

/* I2S DMA Stream definitions */
#define I2S3_DMAx_CLK_ENABLE() __HAL_RCC_DMA1_CLK_ENABLE()
#define I2S3_DMAx_CLK_DISABLE() __HAL_RCC_DMA1_CLK_DISABLE()
#define I2S3_DMAx_STREAM DMA1_Stream5
#define I2S3_DMAx_CHANNEL DMA_CHANNEL_0
#define I2S3_DMAx_IRQ DMA1_Stream5_IRQn
#define I2S3_DMAx_PERIPH_DATA_SIZE DMA_PDATAALIGN_HALFWORD
#define I2S3_DMAx_MEM_DATA_SIZE DMA_MDATAALIGN_HALFWORD
#define DMA_MAX_SZE 0xFFFF

#define I2S3_IRQHandler DMA1_Stream5_IRQHandler

/* Select the interrupt preemption priority and subpriority for the DMA
 * interrupt */
#define AUDIO_OUT_IRQ_PREPRIO \
  6U /* FreeRTOS FromISR API 可用，且高於 UART/EXTI。 */

/*------------------------------------------------------------------------------
             CONFIGURATION: Audio Driver Configuration parameters
------------------------------------------------------------------------------*/

#define AUDIODATA_SIZE 2 /* 16-bits audio data size */

/* Audio status definition */
#define AUDIO_OK 0
#define AUDIO_ERROR 1
#define AUDIO_TIMEOUT 2

#define DMA_MAX(_X_) (((_X_) <= DMA_MAX_SZE) ? (_X_) : DMA_MAX_SZE)
#define HTONS(A) \
  ((((uint16_t)(A) & 0xff00) >> 8) | (((uint16_t)(A) & 0x00ff) << 8))

uint8_t AUDIO_OUT_Init(uint16_t OutputDevice, uint8_t Volume,
                       uint32_t AudioFreq);
uint8_t AUDIO_OUT_Play(uint16_t *pBuffer, uint32_t Size);
void AUDIO_OUT_ChangeBuffer(uint16_t *pData, uint16_t Size);
uint8_t AUDIO_OUT_Pause(void);
uint8_t AUDIO_OUT_Resume(void);
uint8_t AUDIO_OUT_Stop(uint32_t Option);
uint8_t AUDIO_OUT_SetVolume(uint8_t Volume);
void AUDIO_OUT_SetFrequency(uint32_t AudioFreq);
uint8_t AUDIO_OUT_SetMute(uint32_t Cmd);
uint8_t AUDIO_OUT_SetOutputMode(uint8_t Output);

/* User Callbacks: user has to implement these functions in his code if they are
 * needed. */
/* This function is called when the requested data has been completely
 * transferred. */
void AUDIO_OUT_TransferComplete_CallBack(void);

/* This function is called when half of the requested buffer has been
 * transferred. */
void AUDIO_OUT_HalfTransfer_CallBack(void);

/* This function is called when an Interrupt due to transfer error on or
   peripheral error occurs. */
void AUDIO_OUT_Error_CallBack(void);

/* These function can be modified in case the current settings (e.g. DMA stream)
   need to be changed for specific application needs */
void AUDIO_OUT_ClockConfig(I2S_HandleTypeDef *hi2s, uint32_t AudioFreq,
                           void *Params);
void AUDIO_OUT_MspInit(I2S_HandleTypeDef *hi2s, void *Params);
void AUDIO_OUT_MspDeInit(I2S_HandleTypeDef *hi2s, void *Params);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
