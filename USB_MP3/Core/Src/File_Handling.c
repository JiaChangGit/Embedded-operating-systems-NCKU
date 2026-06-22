/**
  ***************************************************************************************************************
  ***************************************************************************************************************
  ***************************************************************************************************************

  File:		  File_Handling.c
  Author:     ControllersTech.com
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

#include "File_Handling.h"

#include "stm32f4xx_hal.h"
#include "usb_host.h"

extern char USBHPath[4]; /* USBH logical drive path */
extern FATFS USBHFatFS;  /* File system object for USBH logical drive */
extern FIL USBHFile;     /* File object for USBH */

FILINFO USBHfno;
FRESULT fresult;  // result
UINT br, bw;      // File read/write count

static uint16_t wav_object_count = 0;
FILELIST_FileTypeDef FileList;

static bool has_wav_extension(const char *file_name) {
  const char *extension = strrchr(file_name, '.');
  if ((extension == NULL) || (strlen(extension) != 4U)) {
    return false;
  }
  return ((extension[1] == 'w') || (extension[1] == 'W')) &&
         ((extension[2] == 'a') || (extension[2] == 'A')) &&
         ((extension[3] == 'v') || (extension[3] == 'V')) &&
         (extension[4] == '\0');
}

FRESULT AUDIO_StorageParse(void) {
  FRESULT res = FR_OK;
  FILINFO fno;
  DIR dir;

  res = f_opendir(&dir, USBHPath);
  FileList.ptr = 0;

  if (res == FR_OK) {
    while (Appli_state == APPLICATION_READY) {
      res = f_readdir(&dir, &fno);
      if (res != FR_OK || fno.fname[0] == 0) {
        break;
      }
      if (fno.fname[0] == '.') {
        continue;
      }

      if (FileList.ptr < FILEMGR_LIST_DEPDTH) {
        if (((fno.fattrib & AM_DIR) == 0) &&
            has_wav_extension((const char *)fno.fname)) {
          size_t name_length = strlen((const char *)fno.fname);
          if (name_length >= FILEMGR_FILE_NAME_SIZE) {
            /*
             * 不可截短檔名後再 f_open()，截短後的路徑並不存在。
             * 超過清單欄位容量的檔案直接略過。
             */
            continue;
          }
          memcpy(FileList.file[FileList.ptr].name, fno.fname, name_length);
          FileList.file[FileList.ptr].name[name_length] = '\0';
          FileList.file[FileList.ptr].type = FILETYPE_FILE;
          FileList.ptr++;
        }
      }
    }
    (void)f_closedir(&dir);
  }

  /*
   * 掃描期間若 USB 被拔除，f_readdir() 可能尚未回傳明確錯誤。
   * 此時不可保留部分清單，也不可把掃描結果當成成功。
   */
  if (Appli_state != APPLICATION_READY) {
    memset(&FileList, 0, sizeof(FileList));
    res = FR_NOT_READY;
  }
  wav_object_count = FileList.ptr;
  return res;
}

uint16_t AUDIO_GetWavObjectNumber(void) { return wav_object_count; }

const char *AUDIO_GetWavName(uint16_t index) {
  if (index >= wav_object_count) {
    return NULL;
  }
  return (const char *)FileList.file[index].name;
}

void AUDIO_ClearFileList(void) {
  memset(&FileList, 0, sizeof(FileList));
  wav_object_count = 0U;
}

FRESULT Mount_USB(void) {
  fresult = f_mount(&USBHFatFS, USBHPath, 1);
  return fresult;
}

FRESULT Unmount_USB(void) {
  fresult = f_mount(NULL, USBHPath, 1);
  return fresult;
}
