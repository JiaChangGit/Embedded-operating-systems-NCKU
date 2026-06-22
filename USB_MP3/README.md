# STM32F407 FreeRTOS USB WAV Player

STM32F407 從 USB 隨身碟讀取 PCM WAV，經由 FatFs、I2S3 DMA 與
CS43L22 輸出音訊。控制介面包含 USART3 CLI、HC-05 相容封包、PA0
按鍵與 1602 I2C LCD；MonitorTask 負責收集 Heap、Stack、Queue、
Heartbeat 與錯誤計數。

## 目前包含

- FreeRTOS 多工作架構（Multi-task Architecture）
- USB OTG FS Host + MSC（Mass Storage Class）
- FatFs 檔案系統
- WAV Header / RIFF Chunk 解析
- I2S3 + DMA Circular Mode
- Ping-pong Audio Buffer
- USART3 Receive-to-Idle DMA
- UART CLI Command Dispatch Table
- Task Heartbeat 與 Runtime Diagnostics
- 選配 IWDG（Independent Watchdog）
- 1602 LCD 狀態顯示

## WAV 格式

目前 parser 接受的音訊格式：

| 項目 | 規格 |
|---|---|
| Container | RIFF / WAVE |
| Codec | PCM |
| Channel | Stereo |
| Sample Format | Signed 16-bit Little-endian |
| Sample Rate | 8、11.025、16、22.05、32、44.1、48、96 kHz |

檔名 `USB_MP3` 是原始工程名稱。韌體沒有 MP3 decoder，USB 內的音檔要先
轉成 PCM WAV。

## 文件

- [建置、接線、CubeMX 與板端驗證](BUILD_AND_BOARD_SETUP.md)
- [FreeRTOS 與 Audio Buffer 架構](FREERTOS_ARCHITECTURE.md)
- [系統流程圖與時序圖](graph_mp3.md)

## 建置

STM32CubeIDE：

1. 以 `Existing Projects into Workspace` 匯入資料夾。
2. 對專案執行 `Refresh`。
3. 執行 `Project > Clean...`。
4. 選擇 `Debug` 或 `Release`。
5. 執行 `Project > Build Project`。

命令列：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 `
  -Configuration Debug `
  -OutputDirectory build-cli-debug
```

FreeRTOS Kernel 10.3.1 已放在
`Middlewares/Third_Party/FreeRTOS/Source`。不要再從 CubeMX 產生第二份
Kernel，否則會出現重複 symbol、重複 handler 或兩份
`FreeRTOSConfig.h`。

## UART CLI

| 設定 | 數值 |
|---|---|
| Peripheral | USART3 |
| TX | PD8 |
| RX | PB11 |
| Baud Rate | 9600 |
| Format | 8-N-1 |
| Flow Control | None |

指令：

```text
help
tasks
heap
stats
audio play
audio pause
audio stop
audio next
audio prev
volume up
volume down
volume set 50
watchdog
buffers
errors
reset_stats
```

## 命令列編譯紀錄

2026 年 6 月 18 日使用 GNU Arm Embedded Toolchain 14.2 與
`tools/build_firmware.ps1` 執行：

```text
Debug   text 109248, data 192, bss 64824
Release text 69556,  data 192, bss 64832
```

該次 Debug 與 Release 命令列建置皆有產生 ELF。Linker 同時回報既有
linker script 的 RWX LOAD segment warning；該次建置沒有因警告中止，但
警告本身仍待調整 linker program headers 後排除。專案自有模組另以
`-Wextra -Wshadow -Wconversion -Wsign-conversion` 與 `-Werror` 編譯，
該次檢查未出現警告。

以上結果只代表該次編譯與連結成功，不等同 USB、Audio、LCD、UART 或 IWDG
已完成板端量測。

## 尚待板端確認

以下項目無法從原始碼與命令列建置結果確認：

- HSE 是否為 8 MHz
- PC0 控制 USB VBUS power switch 的極性
- USB 5 V 電源可供應的電流
- Audio codec 是否為 CS43L22
- LCD backpack 的 I2C address 與 pin mapping
- HC-05 或 USB-TTL 的 UART logic level
