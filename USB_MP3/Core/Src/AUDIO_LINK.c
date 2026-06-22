/**
 ******************************************************************************
 * @file    stm32f411e_discovery.c
 * @author  MCD Application Team
 * @brief   This file provides set of firmware functions to manage LEDs and
 *          push-button available on STM32F411-Discovery Kit from
 *STMicroelectronics.
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

  File:		  AUDIO_LINK.c
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
#include "AUDIO_LINK.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t I2cxTimeout =
    I2Cx_TIMEOUT_MAX; /*<! Value of Timeout when I2C communication fails */

static I2C_HandleTypeDef I2cHandle;

static void audio_link_delay_ms(uint32_t delay_ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  } else {
    HAL_Delay(delay_ms);
  }
}

/* I2Cx bus function */
static void I2Cx_Init(void);
static HAL_StatusTypeDef I2Cx_WriteData(uint16_t Addr, uint8_t Reg,
                                       uint8_t Value);
static uint8_t I2Cx_ReadData(uint16_t Addr, uint8_t Reg);
static void I2Cx_Error(void);
static void I2Cx_MspInit(I2C_HandleTypeDef *hi2c);

/* Link functions for AUDIO */
void AUDIO_IO_Init(void);
void AUDIO_IO_DeInit(void);
uint8_t AUDIO_IO_Write(uint16_t Addr, uint8_t Reg, uint8_t Value);
uint8_t AUDIO_IO_Read(uint16_t Addr, uint8_t Reg);

/*******************************************************************************
                            BUS OPERATIONS
*******************************************************************************/

/******************************* I2C Routines *********************************/

/**
 * @brief  I2Cx Bus initialization.
 */
static void I2Cx_Init(void) {
  if (HAL_I2C_GetState(&I2cHandle) == HAL_I2C_STATE_RESET) {
    I2cHandle.Instance = AUDIO_I2Cx;
    I2cHandle.Init.OwnAddress1 = 0x43;
    I2cHandle.Init.ClockSpeed = I2Cx_MAX_COMMUNICATION_FREQ;
    I2cHandle.Init.DutyCycle = I2C_DUTYCYCLE_2;
    I2cHandle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    I2cHandle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLED;
    I2cHandle.Init.OwnAddress2 = 0x00;
    I2cHandle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLED;
    I2cHandle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLED;

    /* Init the I2C */
    I2Cx_MspInit(&I2cHandle);
    HAL_I2C_Init(&I2cHandle);
  }
}

/**
 * @brief  Writes a value in a register of the device through BUS.
 * @param  Addr: Device address on BUS Bus.
 * @param  Reg: The target register address to write
 * @param  Value: The target register value to be written
 */
static HAL_StatusTypeDef I2Cx_WriteData(uint16_t Addr, uint8_t Reg,
                                       uint8_t Value) {
  HAL_StatusTypeDef status = HAL_OK;

  status = HAL_I2C_Mem_Write(&I2cHandle, Addr, (uint16_t)Reg,
                             I2C_MEMADD_SIZE_8BIT, &Value, 1, I2cxTimeout);

  /* 匯流排錯誤時重設 I2C 後只重試一次，避免 task 長時間卡住。 */
  if (status != HAL_OK) {
    I2Cx_Error();
    status = HAL_I2C_Mem_Write(&I2cHandle, Addr, (uint16_t)Reg,
                               I2C_MEMADD_SIZE_8BIT, &Value, 1, I2cxTimeout);
  }
  return status;
}

/**
 * @brief  Reads a register of the device through BUS.
 * @param  Addr: Device address on BUS Bus.
 * @param  Reg: The target register address to write
 * @retval Data read at register address
 */
static uint8_t I2Cx_ReadData(uint16_t Addr, uint8_t Reg) {
  HAL_StatusTypeDef status = HAL_OK;
  uint8_t value = 0;

  status = HAL_I2C_Mem_Read(&I2cHandle, Addr, Reg, I2C_MEMADD_SIZE_8BIT, &value,
                            1, I2cxTimeout);

  /* Check the communication status */
  if (status != HAL_OK) {
    I2Cx_Error();
    status = HAL_I2C_Mem_Read(&I2cHandle, Addr, Reg, I2C_MEMADD_SIZE_8BIT,
                              &value, 1, I2cxTimeout);
    if (status != HAL_OK) {
      value = 0U;
    }
  }
  return value;
}

/**
 * @brief  I2Cx error treatment function.
 */
static void I2Cx_Error(void) {
  /* De-initialize the I2C comunication BUS */
  HAL_I2C_DeInit(&I2cHandle);

  /* Re- Initiaize the I2C comunication BUS */
  I2Cx_Init();
}

/**
 * @brief  I2Cx MSP Init.
 * @param  hi2c: I2C handle
 */
static void I2Cx_MspInit(I2C_HandleTypeDef *hi2c) {
  GPIO_InitTypeDef GPIO_InitStructure;
  (void)hi2c;

  /* Enable the I2C peripheral */
  AUDIO_I2Cx_CLOCK_ENABLE();

  /* Enable SCK and SDA GPIO clocks */
  AUDIO_I2Cx_GPIO_CLK_ENABLE();

  /* I2Cx SD1 & SCK pin configuration */
  GPIO_InitStructure.Pin = AUDIO_I2Cx_SDA_PIN | AUDIO_I2Cx_SCL_PIN;
  GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Speed = GPIO_SPEED_FAST;
  GPIO_InitStructure.Alternate = AUDIO_I2Cx_AF;

  HAL_GPIO_Init(AUDIO_I2Cx_GPIO_PORT, &GPIO_InitStructure);

  /* Force the I2C peripheral clock reset */
  AUDIO_I2Cx_FORCE_RESET();

  /* Release the I2C peripheral clock reset */
  AUDIO_I2Cx_RELEASE_RESET();

  /*
   * Codec control 使用 blocking HAL_I2C_Mem_Read/Write，不啟用 I2C1 IRQ。
   * 專案沒有 I2C1_EV/ER handler，若開啟 NVIC 可能落入 Default_Handler。
   */
  HAL_NVIC_DisableIRQ(AUDIO_I2Cx_EV_IRQn);
  HAL_NVIC_DisableIRQ(AUDIO_I2Cx_ER_IRQn);
}

/*******************************************************************************
                            LINK OPERATIONS
*******************************************************************************/

/********************************* LINK AUDIO *********************************/

/**
 * @brief  Initializes Audio low level.
 */
void AUDIO_IO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct;

  /* Enable Reset GPIO Clock */
  AUDIO_RESET_GPIO_CLK_ENABLE();

  /* Audio reset pin configuration -------------------------------------------*/
  GPIO_InitStruct.Pin = AUDIO_RESET_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FAST;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(AUDIO_RESET_GPIO, &GPIO_InitStruct);

  I2Cx_Init();

  /* Power Down the codec */
  CODEC_AUDIO_POWER_OFF();

  /* Wait for a delay to insure registers erasing */
  audio_link_delay_ms(5U);

  /* Power on the codec */
  CODEC_AUDIO_POWER_ON();

  /* Wait for a delay to insure registers erasing */
  audio_link_delay_ms(5U);
}

/**
 * @brief  DeInitializes Audio low level.
 */
void AUDIO_IO_DeInit(void) {}

/**
 * @brief  Writes a single data.
 * @param  Addr: I2C address
 * @param  Reg: Reg address
 * @param  Value: Data to be written
 */
uint8_t AUDIO_IO_Write(uint16_t Addr, uint8_t Reg, uint8_t Value) {
  return (uint8_t)((I2Cx_WriteData(Addr, Reg, Value) == HAL_OK) ? 0U : 1U);
}

/**
 * @brief  Reads a single data.
 * @param  Addr: I2C address
 * @param  Reg: Reg address
 * @retval Data to be read
 */
uint8_t AUDIO_IO_Read(uint16_t Addr, uint8_t Reg) {
  return I2Cx_ReadData(Addr, Reg);
}
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
