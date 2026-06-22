/**
 ******************************************************************************
 * @file    stm32f411e_discovery.h
 * @author  MCD Application Team
 * @brief   This file contains definitions for STM32F401-Discovery Kit's Leds
 *and push-button hardware resources.
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

  File:		  AUDIO_LINK.h
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
#ifndef __AUDIO_LINK_H
#define __AUDIO_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/*############################### I2Cx #######################################*/
#define AUDIO_I2Cx I2C1
#define AUDIO_I2Cx_CLOCK_ENABLE() __HAL_RCC_I2C1_CLK_ENABLE()
#define AUDIO_I2Cx_GPIO_PORT GPIOB    /* GPIOB */
#define AUDIO_I2Cx_SCL_PIN GPIO_PIN_6 /* PB.06 */
#define AUDIO_I2Cx_SDA_PIN GPIO_PIN_9 /* PB.09 */
#define AUDIO_I2Cx_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define AUDIO_I2Cx_GPIO_CLK_DISABLE() __HAL_RCC_GPIOB_CLK_DISABLE()
#define AUDIO_I2Cx_AF GPIO_AF4_I2C1

#define AUDIO_I2Cx_FORCE_RESET() __HAL_RCC_I2C1_FORCE_RESET()
#define AUDIO_I2Cx_RELEASE_RESET() __HAL_RCC_I2C1_RELEASE_RESET()

/* I2C interrupt requests */
#define AUDIO_I2Cx_EV_IRQn I2C1_EV_IRQn
#define AUDIO_I2Cx_ER_IRQn I2C1_ER_IRQn

/* I2C speed and timeout max */
#define I2Cx_TIMEOUT_MAX 100U
#define I2Cx_MAX_COMMUNICATION_FREQ ((uint32_t)100000)

/*################################### AUDIO ##################################*/
/**
 * @brief  AUDIO I2C Interface pins
 */
/* Device I2C address */
#define AUDIO_I2C_ADDRESS 0x94

/* Audio codec power on/off macro definition */
#define CODEC_AUDIO_POWER_OFF() \
  HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_RESET)
#define CODEC_AUDIO_POWER_ON() \
  HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_SET)

/* Audio Reset Pin definition */
#define AUDIO_RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
#define AUDIO_RESET_PIN GPIO_PIN_4
#define AUDIO_RESET_GPIO GPIOD

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_LINK_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
