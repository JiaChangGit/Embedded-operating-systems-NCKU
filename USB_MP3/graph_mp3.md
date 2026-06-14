# USB_MP3 程式碼圖解

這份只看程式碼，不拿既有文件來補腦。先講一個容易誤會的地方：專案名稱是 `USB_MP3`，但程式碼實際掃的是 `wav` / `WAV`，也用 `WAVE_FormatTypeDef` 讀 WAV header，沒有看到 MP3 解碼流程。

讀這包程式時，可以先抓三個重點：

1. `main()` 負責等 USB 隨身碟 ready、掛載檔案系統、啟動播放器。
2. `waveplayer.c` 負責找 WAV、讀檔、補 buffer、處理播放狀態。
3. `AUDIO.c`、`cs43l22.c`、`AUDIO_LINK.c` 負責把 buffer 送到 I2S DMA，再透過 codec 發出聲音。

## 01. 一眼看懂整個專案

```mermaid
flowchart LR
    User["使用者"] -->|"按鍵 PA0"| EXTI["EXTI0 中斷"]
    Phone["手機/藍牙控制端"] -->|"UART3 封包"| UART["USART3 + DMA"]
    USB["USB 隨身碟"] -->|"USB MSC"| USBH["USB Host Library"]
    USBH --> FATFS["FatFs"]
    FATFS --> FileHandling["File_Handling.c<br/>掃 WAV 清單"]
    FileHandling --> WavePlayer["waveplayer.c<br/>播放器狀態機"]
    UART -->|"改 AudioState"| WavePlayer
    EXTI -->|"改 AudioState"| WavePlayer
    WavePlayer --> Audio["AUDIO.c<br/>音訊輸出 API"]
    Audio --> Codec["cs43l22.c<br/>Codec 控制"]
    Audio --> I2S["I2S3 + DMA<br/>PCM 資料"]
    Codec -->|"I2C1 暫存器"| Chip["CS43L22"]
    I2S -->|"WS/SCK/SD/MCK"| Chip
    WavePlayer --> LCD["LCD16x2<br/>I2C3 顯示狀態"]
```

## 02. 程式分層

```mermaid
flowchart TB
    App["應用層<br/>main.c"]
    Player["播放邏輯<br/>waveplayer.c<br/>File_Handling.c"]
    Board["板級音訊層<br/>AUDIO.c<br/>AUDIO_LINK.c<br/>cs43l22.c<br/>lcd16x2_i2c.h"]
    Middleware["中介層<br/>FatFs<br/>USB Host MSC"]
    HAL["STM32 HAL<br/>I2C / I2S / UART / DMA / HCD / GPIO"]
    HW["硬體<br/>STM32F407 + USB 隨身碟 + CS43L22 + LCD + 藍牙模組"]

    App --> Player
    App --> Board
    Player --> Middleware
    Player --> Board
    Board --> HAL
    Middleware --> HAL
    HAL --> HW
```

## 03. 主要檔案分工

```mermaid
flowchart LR
    Main["main.c<br/>系統初始化<br/>主迴圈<br/>UART/按鍵 callback"]
    USBHost["usb_host.c<br/>USB Host 初始化<br/>Appli_state 切換"]
    Fatfs["fatfs.c<br/>FATFS_LinkDriver<br/>USBHPath"]
    Diskio["usbh_diskio.c<br/>FatFs 到 USB MSC 的橋"]
    File["File_Handling.c<br/>Mount_USB<br/>AUDIO_StorageParse"]
    Wave["waveplayer.c<br/>AUDIO_PLAYER_Start<br/>AUDIO_PLAYER_Process"]
    Audio["AUDIO.c<br/>AUDIO_OUT_*<br/>I2S DMA callback"]
    Link["AUDIO_LINK.c<br/>AUDIO_IO_Read/Write<br/>I2C1 傳輸"]
    Codec["cs43l22.c<br/>codec 暫存器設定"]
    LCD["lcd16x2_i2c.h<br/>LCD 初始化與 printf"]
    MSP["stm32f4xx_hal_msp.c<br/>周邊腳位/DMA 初始化"]
    IT["stm32f4xx_it.c<br/>IRQ 進 HAL handler"]

    Main --> USBHost
    Main --> Fatfs
    Main --> File
    Main --> Wave
    Main --> LCD
    Wave --> File
    Wave --> Audio
    File --> Fatfs
    Fatfs --> Diskio
    Diskio --> USBHost
    Audio --> Codec
    Codec --> Link
    Audio --> MSP
    IT --> Main
    IT --> Audio
```

## 04. 上電到主迴圈的順序

```mermaid
flowchart TD
    Reset["Reset / 進 main()"] --> HALInit["HAL_Init()"]
    HALInit --> Clock["SystemClock_Config()"]
    Clock --> GPIO["MX_GPIO_Init()"]
    GPIO --> DMA["MX_DMA_Init()"]
    DMA --> I2C1["MX_I2C1_Init()<br/>給 codec 控制用"]
    I2C1 --> I2S3["MX_I2S3_Init()<br/>給音訊資料用"]
    I2S3 --> FATFSInit["MX_FATFS_Init()<br/>連 USBH_Driver"]
    FATFSInit --> USBInit["MX_USB_HOST_Init()<br/>註冊 MSC class"]
    USBInit --> I2C3["MX_I2C3_Init()<br/>給 LCD 用"]
    I2C3 --> UART3["MX_USART3_UART_Init()<br/>藍牙控制用"]
    UART3 --> LCDInit["lcd16x2_i2c_init(&hi2c3)<br/>顯示 START"]
    LCDInit --> UARTPrime["HAL_UARTEx_RxEventCallback(&huart3, 20)<br/>等於先啟動 ReceiveToIdle DMA"]
    UARTPrime --> Volume["uwVolume_tmp = get_uwVolume()"]
    Volume --> Loop["while(1)"]
```

## 05. 主迴圈在等什麼

```mermaid
flowchart TD
    Loop["while(1)"] --> USBProcess["MX_USB_HOST_Process()"]
    USBProcess --> Ready{"Appli_state == APPLICATION_READY ?"}
    Ready -- "否" --> Loop
    Ready -- "是" --> Mount["Mount_USB()"]
    Mount --> Start["AUDIO_PLAYER_Start(0)"]
    Start --> LED13["PD13 = SET"]
    LED13 --> PlayLoop{"IsFinished == 0 ?"}
    PlayLoop -- "是" --> Process["AUDIO_PLAYER_Process(TRUE)"]
    Process --> MainUI["main.c 根據 AudioState 更新 LCD / LED"]
    MainUI --> PlayLoop
    PlayLoop -- "否" --> Loop
```

## 06. USB Host 狀態怎麼進 main

```mermaid
stateDiagram-v2
    [*] --> APPLICATION_IDLE
    APPLICATION_IDLE --> APPLICATION_START: HOST_USER_CONNECTION
    APPLICATION_START --> APPLICATION_READY: HOST_USER_CLASS_ACTIVE
    APPLICATION_READY --> APPLICATION_DISCONNECT: HOST_USER_DISCONNECTION
    APPLICATION_DISCONNECT --> APPLICATION_START: HOST_USER_CONNECTION
```

`Appli_state` 是 `usb_host.c` 的全域變數，`main.c` 只看它是不是 `APPLICATION_READY`。真正切換是在 `USBH_UserProcess()` callback。

## 07. USB 隨身碟讀檔路線

```mermaid
flowchart LR
    WavRead["waveplayer.c<br/>f_read(&WavFile, ...)"]
    FatFs["FatFs<br/>ff.c"]
    Diskio["DiskIO adapter<br/>usbh_diskio.c"]
    MSC["USBH_MSC_Read()"]
    USBCore["USB Host Core<br/>USBH_Process / pipes"]
    HCD["HAL HCD<br/>USB_OTG_FS"]
    Flash["USB 隨身碟"]

    WavRead --> FatFs
    FatFs --> Diskio
    Diskio --> MSC
    MSC --> USBCore
    USBCore --> HCD
    HCD --> Flash
```

## 08. 掃 WAV 清單

```mermaid
flowchart TD
    Parse["AUDIO_StorageParse()"] --> OpenDir["f_opendir(&dir, USBHPath)"]
    OpenDir --> ResetList["FileList.ptr = 0"]
    ResetList --> Loop{"Appli_state 還是 READY ?"}
    Loop -- "否" --> Close["f_closedir(&dir)"]
    Loop -- "是" --> ReadDir["f_readdir(&dir, &fno)"]
    ReadDir --> End{"讀完或錯誤 ?"}
    End -- "是" --> SaveNum["NumObs = FileList.ptr"]
    SaveNum --> Close
    End -- "否" --> Dot{"檔名第一個字是 . ?"}
    Dot -- "是" --> Loop
    Dot -- "否" --> IsFile{"不是資料夾 ?"}
    IsFile -- "否" --> Loop
    IsFile -- "是" --> IsWav{"檔名含 wav 或 WAV ?"}
    IsWav -- "否" --> Loop
    IsWav -- "是" --> Room{"FileList.ptr < 24 ?"}
    Room -- "否" --> Loop
    Room -- "是" --> Add["複製檔名到 FileList.file[ptr]<br/>type = FILETYPE_FILE<br/>ptr++"]
    Add --> Loop
```

## 09. 播第一首的時序

```mermaid
sequenceDiagram
    participant Main as main.c
    participant File as File_Handling.c
    participant Wave as waveplayer.c
    participant Fat as FatFs
    participant Audio as AUDIO.c
    participant Codec as cs43l22.c
    participant DMA as I2S DMA

    Main->>File: Mount_USB()
    File->>Fat: f_mount(&USBHFatFS, USBHPath, 1)
    Main->>Wave: AUDIO_PLAYER_Start(0)
    Wave->>File: AUDIO_GetWavObjectNumber()
    File->>Fat: f_opendir / f_readdir
    File-->>Wave: WAV 數量
    Wave->>Fat: f_open(FileList.file[0].name)
    Wave->>Fat: f_read(&WaveFormat, sizeof(WaveFormat))
    Wave->>Audio: AUDIO_OUT_Init(BOTH, volume, WaveFormat.SampleRate)
    Audio->>Codec: ReadID / Init / SetVolume
    Wave->>Fat: f_lseek(&WavFile, 0)
    Wave->>Fat: f_read(BufferCtl.buff, 4096)
    Wave->>Audio: AUDIO_OUT_Play(buffer, 4096)
    Audio->>Codec: Play()
    Audio->>DMA: HAL_I2S_Transmit_DMA()
```

## 10. 音訊資料流

```mermaid
flowchart LR
    File["USB 裡的 WAV 檔"] -->|"f_read"| Buffer["BufferCtl.buff[4096]"]
    Buffer -->|"uint16_t* 指標"| AudioOut["AUDIO_OUT_Play()"]
    AudioOut -->|"HAL_I2S_Transmit_DMA"| DMA["DMA<br/>memory to I2S3"]
    DMA -->|"16-bit PCM"| I2S["I2S3<br/>WS/SCK/SD/MCK"]
    I2S --> Codec["CS43L22 codec"]
    Codec --> Speaker["喇叭 / 耳機"]
```

## 11. 4096 bytes buffer 怎麼輪流補

```mermaid
flowchart TD
    DMAStart["DMA 開始送 BufferCtl.buff[0..4095]"] --> FirstHalf["前半段被 DMA 送完"]
    FirstHalf --> HalfCB["HAL_I2S_TxHalfCpltCallback()"]
    HalfCB --> SetHalf["AUDIO_OUT_HalfTransfer_CallBack()<br/>BufferCtl.state = BUFFER_OFFSET_HALF"]
    SetHalf --> ProcessHalf["AUDIO_PLAYER_Process()<br/>f_read 到 buff[0..2047]"]
    ProcessHalf --> SecondHalf["後半段被 DMA 送完"]
    SecondHalf --> FullCB["HAL_I2S_TxCpltCallback()"]
    FullCB --> SetFull["AUDIO_OUT_TransferComplete_CallBack()<br/>BufferCtl.state = BUFFER_OFFSET_FULL"]
    SetFull --> ProcessFull["AUDIO_PLAYER_Process()<br/>f_read 到 buff[2048..4095]"]
    ProcessFull --> FirstHalf
```

## 12. DMA callback 和主迴圈的關係

```mermaid
sequenceDiagram
    participant DMA as DMA1 Stream
    participant HAL as HAL I2S
    participant Audio as AUDIO.c callback
    participant Wave as waveplayer.c
    participant Main as main while loop
    participant Fat as FatFs

    DMA-->>HAL: half transfer interrupt
    HAL-->>Audio: HAL_I2S_TxHalfCpltCallback()
    Audio-->>Wave: AUDIO_OUT_HalfTransfer_CallBack()
    Wave-->>Wave: BufferCtl.state = HALF
    Main->>Wave: AUDIO_PLAYER_Process(TRUE)
    Wave->>Fat: f_read(buff 前半段)
    Wave-->>Wave: BufferCtl.state = NONE

    DMA-->>HAL: transfer complete interrupt
    HAL-->>Audio: HAL_I2S_TxCpltCallback()
    Audio-->>Wave: AUDIO_OUT_TransferComplete_CallBack()
    Wave-->>Wave: BufferCtl.state = FULL
    Main->>Wave: AUDIO_PLAYER_Process(TRUE)
    Wave->>Fat: f_read(buff 後半段)
    Wave-->>Wave: BufferCtl.state = NONE
```

## 13. 播放狀態機總圖

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> PLAY: AUDIO_PLAYER_Start()
    PLAY --> NEXT: 檔案播放到尾端
    PLAY --> NEXT: 藍牙 0x00 或 PA0 按鍵
    PLAY --> PREVIOUS: 藍牙 0x01
    PLAY --> PAUSE: 藍牙 0x04
    PAUSE --> WAIT: AUDIO_OUT_Pause()
    WAIT --> RESUME: 藍牙 0x05
    RESUME --> PLAY: AUDIO_OUT_Resume()
    PLAY --> VOLUME_UP: 藍牙 0x02
    PLAY --> VOLUME_DOWN: 藍牙 0x03
    VOLUME_UP --> PLAY: SetVolume 後
    VOLUME_DOWN --> PLAY: SetVolume 後
    NEXT --> PLAY: AUDIO_PLAYER_Start(FilePos)
    PREVIOUS --> PLAY: AUDIO_PLAYER_Start(FilePos)
    NEXT --> STOP: 不 loop 且超過最後一首
    STOP --> IDLE: AUDIO_OUT_Stop()
```

`main.c` 呼叫 `AUDIO_PLAYER_Process(TRUE)`，所以正常播到最後一首時，`NEXT` 會繞回第 0 首，不會自己停。`STOP` 主要是程式其他地方把 `AudioState` 設成 stop 才會走到。

## 14. `AUDIO_PLAYER_Process(TRUE)` 的分支

```mermaid
flowchart TD
    Proc["AUDIO_PLAYER_Process(isLoop)"] --> State{"AudioState"}
    State -->|"PLAY"| Play["檢查檔案尾端<br/>看 BufferCtl.state 補半段資料"]
    State -->|"STOP"| Stop["AUDIO_OUT_Stop()<br/>AudioState = IDLE<br/>回傳 AUDIO_ERROR_IO"]
    State -->|"NEXT"| Next["FilePos++<br/>必要時繞回 0<br/>Stop 後 Start(FilePos)"]
    State -->|"PREVIOUS"| Prev["FilePos--<br/>小於 0 就跳最後一首<br/>Stop 後 Start(FilePos)"]
    State -->|"PAUSE"| Pause["AUDIO_OUT_Pause()<br/>AudioState = WAIT"]
    State -->|"RESUME"| Resume["AUDIO_OUT_Resume()<br/>AudioState = PLAY"]
    State -->|"VOLUME_UP"| VolUp["uwVolume += 10, 上限判斷<br/>AUDIO_OUT_SetVolume()<br/>AudioState = PLAY"]
    State -->|"VOLUME_DOWN"| VolDown["uwVolume -= 10, 下限判斷<br/>AUDIO_OUT_SetVolume()<br/>AudioState = PLAY"]
    State -->|"WAIT / IDLE / INIT / default"| None["不做事"]
```

## 15. 下一首流程

```mermaid
sequenceDiagram
    participant Ctrl as 控制來源
    participant Main as main.c callback
    participant Wave as waveplayer.c
    participant Audio as AUDIO.c
    participant Fat as FatFs

    Ctrl->>Main: 藍牙 0x00 或 PA0 按鍵
    Main->>Wave: AudioState = AUDIO_STATE_NEXT
    Main->>Wave: AUDIO_PLAYER_Process(TRUE)
    Wave->>Wave: ++FilePos
    Wave->>Wave: 若超過 WAV 數量且 isLoop=true，FilePos=0
    Wave->>Audio: AUDIO_OUT_Stop(CODEC_PDWN_SW)
    Wave->>Wave: AUDIO_PLAYER_Start(FilePos)
    Wave->>Fat: f_open / f_read header / f_read buffer
    Wave->>Audio: AUDIO_OUT_Play()
```

## 16. 暫停與恢復

```mermaid
flowchart LR
    PauseCmd["藍牙命令 0x04"] --> PauseState["AudioState = PAUSE"]
    PauseState --> ProcessPause["AUDIO_PLAYER_Process()"]
    ProcessPause --> OutPause["AUDIO_OUT_Pause()"]
    OutPause --> CodecMute["cs43l22_Pause()<br/>Mute + power save"]
    OutPause --> DMAPause["HAL_I2S_DMAPause()"]
    DMAPause --> Wait["AudioState = WAIT"]

    ResumeCmd["藍牙命令 0x05"] --> ResumeState["AudioState = RESUME"]
    ResumeState --> ProcessResume["AUDIO_PLAYER_Process()"]
    ProcessResume --> OutResume["AUDIO_OUT_Resume()"]
    OutResume --> CodecResume["cs43l22_Resume()<br/>Unmute + power on"]
    OutResume --> DMAResume["HAL_I2S_DMAResume()"]
    DMAResume --> Play["AudioState = PLAY"]
```

## 17. 音量控制

```mermaid
flowchart TD
    Cmd{"AudioState"} -->|"VOLUME_UP"| Up["waveplayer.c<br/>uwVolume <= 90 時 +10"]
    Cmd -->|"VOLUME_DOWN"| Down["waveplayer.c<br/>uwVolume >= 10 時 -10"]
    Up --> Set["AUDIO_OUT_SetVolume(uwVolume)"]
    Down --> Set
    Set --> Codec["pAudioDrv->SetVolume()<br/>cs43l22_SetVolume()"]
    Codec --> Convert["VOLUME_CONVERT(Volume)<br/>0..100 轉 codec 值"]
    Convert --> Reg["寫 CS43L22_REG_MASTER_A_VOL<br/>寫 CS43L22_REG_MASTER_B_VOL"]
    Reg --> Back["AudioState = PLAY"]
```

`main.c` 另外有一個 `uwVolume_tmp` 只拿來顯示 LCD，不是 codec 真正音量來源。真正送到 codec 的是 `waveplayer.c` 裡面的 static `uwVolume`。

## 18. 藍牙 UART 封包怎麼變成動作

```mermaid
flowchart TD
    Idle["USART3 ReceiveToIdle DMA"] --> RxCB["HAL_UARTEx_RxEventCallback(huart, Size)"]
    RxCB --> IsUart3{"huart == &huart3 ?"}
    IsUart3 -- "否" --> End["結束"]
    IsUart3 -- "是" --> Echo["HAL_UART_Transmit_DMA()<br/>把收到的資料回傳"]
    Echo --> Header{"receiveData[0] == 0xAA ?"}
    Header -- "否" --> Invalid["isValid_ble = 0"]
    Header -- "是" --> SizeOk{"receiveData[1] == Size ?"}
    SizeOk -- "否" --> Invalid
    SizeOk -- "是" --> Sum{"sum == receiveData[Size-1] ?"}
    Sum -- "否" --> Invalid
    Sum -- "是" --> Each["掃 receiveData[2..Size-2]"]
    Each --> Switch["依 command 改 AudioState"]
    Switch --> Valid["isValid_ble = 1"]
    Valid --> ReArm["HAL_UARTEx_ReceiveToIdle_DMA()"]
    Invalid --> ReArm
    ReArm --> HTOff["關掉 RX DMA half transfer interrupt"]
    HTOff --> LED{"isValid_ble ?"}
    LED -- "是" --> LEDOff["PD14 = RESET"]
    LED -- "否" --> LEDOn["PD14 = SET"]
```

照目前程式碼，`sum` 被設成 0，累加那段被註解掉，所以封包最後一個 byte 要是 0，才會進到 command switch。

## 19. 藍牙命令對照

```mermaid
flowchart LR
    C0["0x00"] --> NEXT["AUDIO_STATE_NEXT"]
    C1["0x01"] --> PREV["AUDIO_STATE_PREVIOUS"]
    C2["0x02"] --> VUP["AUDIO_STATE_VOLUME_UP"]
    C3["0x03"] --> VDOWN["AUDIO_STATE_VOLUME_DOWN"]
    C4["0x04"] --> PAUSE["AUDIO_STATE_PAUSE"]
    C5["0x05"] --> RESUME["AUDIO_STATE_RESUME"]
```

## 20. 按鍵訊號路徑

```mermaid
sequenceDiagram
    participant Button as PA0 按鍵
    participant EXTI as EXTI0_IRQHandler
    participant HAL as HAL_GPIO_EXTI_IRQHandler
    participant Main as HAL_GPIO_EXTI_Callback
    participant Wave as waveplayer.c 狀態機

    Button-->>EXTI: rising edge
    EXTI-->>HAL: HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)
    HAL-->>Main: HAL_GPIO_EXTI_Callback(GPIO_PIN_0)
    Main->>Main: 若 AudioState == PLAY
    Main->>Wave: AudioState = AUDIO_STATE_NEXT
    Wave->>Wave: 下一次 AUDIO_PLAYER_Process() 切歌
```

## 21. LCD 顯示不是主流程，只是狀態提示

```mermaid
flowchart TD
    LCDInit["lcd16x2_i2c_init(&hi2c3) 成功"] --> StartText["顯示 START / NCKU Team-14"]
    StartText --> LED12["PD12 = SET"]
    Playing["播放中主迴圈"] --> WatchState{"main.c 看到 AudioState"}
    WatchState -->|"STOP"| EndText["顯示 END<br/>PD12/PD13/PD14 reset<br/>IsFinished=1"]
    WatchState -->|"VOLUME_UP"| UpText["顯示 VOLUME_UP 和 uwVolume_tmp"]
    WatchState -->|"VOLUME_DOWN"| DownText["顯示 VOLUME_DOWN 和 uwVolume_tmp"]
    WatchState -->|"RESUME"| ResumeText["顯示 RESUME 和 uwVolume_tmp"]
    WatchState -->|"PAUSE"| PauseText["顯示 PAUSE"]
```

## 22. I2C 兩條線各做各的事

```mermaid
flowchart LR
    subgraph I2C1["I2C1: codec 控制"]
        PB6["PB6 SCL"] --> CodecBus["AUDIO_LINK.c"]
        PB9["PB9 SDA"] --> CodecBus
        CodecBus --> CS43["CS43L22 暫存器"]
    end

    subgraph I2C3["I2C3: LCD 控制"]
        PA8["PA8 SCL"] --> LCDBus["lcd16x2_i2c.h"]
        PC9["PC9 SDA"] --> LCDBus
        LCDBus --> LCD["LCD16x2"]
    end
```

## 23. I2S 音訊腳位

```mermaid
flowchart LR
    DMA["DMA buffer"] --> I2S3["SPI3 / I2S3 Master TX"]
    I2S3 --> PA4["PA4 I2S3_WS"]
    I2S3 --> PC7["PC7 I2S3_MCK"]
    I2S3 --> PC10["PC10 I2S3_CK"]
    I2S3 --> PC12["PC12 I2S3_SD"]
    PA4 --> Codec["CS43L22"]
    PC7 --> Codec
    PC10 --> Codec
    PC12 --> Codec
```

## 24. 其他 GPIO 與 DMA

```mermaid
flowchart TB
    PC0["PC0<br/>USB VBUS 開關"] --> VBUS["MX_DriverVbusFS()"]
    PA0["PA0<br/>EXTI rising"] --> Button["切下一首"]
    PD12["PD12"] --> LEDStart["LCD init 成功 / 播放結束時關"]
    PD13["PD13"] --> LEDPlay["開始播放時亮"]
    PD14["PD14"] --> LEDBle["藍牙封包錯誤亮<br/>正確關"]
    PD15["PD15"] --> LEDUnused["程式碼有初始化<br/>目前沒看到使用"]

    DMA1S1["DMA1 Stream1"] --> UARTRX["USART3 RX"]
    DMA1S3["DMA1 Stream3"] --> UARTTX["USART3 TX"]
    DMA1S5["DMA1 Stream5"] --> I2STX["SPI3 TX / I2S3 TX"]
```

## 25. 中斷總表

```mermaid
flowchart TD
    EXTI0["EXTI0_IRQHandler"] --> GPIOHAL["HAL_GPIO_EXTI_IRQHandler"]
    GPIOHAL --> ButtonCB["HAL_GPIO_EXTI_Callback in main.c"]

    DMA1S1["DMA1_Stream1_IRQHandler"] --> UARTRXHAL["HAL_DMA_IRQHandler(&hdma_usart3_rx)"]
    USART3["USART3_IRQHandler"] --> UARTHAL["HAL_UART_IRQHandler(&huart3)"]
    UARTHAL --> UARTCB["HAL_UARTEx_RxEventCallback in main.c"]

    DMA1S3["DMA1_Stream3_IRQHandler"] --> UARTTXHAL["HAL_DMA_IRQHandler(&hdma_usart3_tx)"]
    DMA1S5["DMA1_Stream5_IRQHandler"] --> I2SHAL["HAL_DMA_IRQHandler(&hdma_spi3_tx)"]
    I2SHAL --> I2SCB["HAL_I2S_TxHalfCpltCallback / TxCpltCallback in AUDIO.c"]

    OTG["OTG_FS_IRQHandler"] --> HCD["HAL_HCD_IRQHandler"]
    HCD --> USBHCB["USBH_LL_* callbacks"]

    I2C3EV["I2C3_EV_IRQHandler"] --> I2C3HAL["HAL_I2C_EV_IRQHandler(&hi2c3)"]
    I2C3ER["I2C3_ER_IRQHandler"] --> I2C3ERR["HAL_I2C_ER_IRQHandler(&hi2c3)"]
```

## 26. Codec 初始化順序

```mermaid
sequenceDiagram
    participant Wave as waveplayer.c
    participant Audio as AUDIO.c
    participant AudioLink as AUDIO_LINK.c
    participant Codec as cs43l22.c
    participant I2C as I2C1 HAL
    participant CS43 as CS43L22

    Wave->>Audio: PlayerInit(WaveFormat.SampleRate)
    Audio->>Audio: AUDIO_OUT_ClockConfig()
    Audio->>Audio: I2S3_Init(AudioFreq)

    Audio->>Codec: cs43l22_drv.ReadID(AUDIO_I2C_ADDRESS)
    Codec->>AudioLink: AUDIO_IO_Init()
    AudioLink->>I2C: I2Cx_Init()
    AudioLink->>CS43: Reset pin OFF then ON

    Codec->>AudioLink: AUDIO_IO_Read(CHIPID)
    AudioLink->>I2C: HAL_I2C_Mem_Read()

    Audio->>Codec: Init(address, BOTH, volume, freq)
    Codec->>AudioLink: AUDIO_IO_Write(reg, value)
    AudioLink->>I2C: HAL_I2C_Mem_Write()
```

## 27. `AUDIO_DrvTypeDef` 函式指標

```mermaid
classDiagram
    class AUDIO_DrvTypeDef {
        +Init()
        +DeInit()
        +ReadID()
        +Play()
        +Pause()
        +Resume()
        +Stop()
        +SetFrequency()
        +SetVolume()
        +SetMute()
        +SetOutputMode()
        +Reset()
    }
    class cs43l22_drv {
        +cs43l22_Init
        +cs43l22_DeInit
        +cs43l22_ReadID
        +cs43l22_Play
        +cs43l22_Pause
        +cs43l22_Resume
        +cs43l22_Stop
        +cs43l22_SetVolume
        +cs43l22_SetMute
        +cs43l22_SetOutputMode
    }
    AUDIO_DrvTypeDef <|.. cs43l22_drv
```

這裡的意思很簡單：`AUDIO.c` 不直接寫死 codec 函式名稱，它透過 `pAudioDrv->Play()` 這種入口呼叫。目前接上的 driver 就是 `cs43l22_drv`。

## 28. 重要資料結構

```mermaid
classDiagram
    class AUDIO_OUT_BufferTypeDef {
        +uint8_t buff[4096]
        +BUFFER_StateTypeDef state
        +uint32_t fptr
    }
    class WAVE_FormatTypeDef {
        +uint32_t ChunkID
        +uint32_t FileSize
        +uint32_t FileFormat
        +uint32_t SampleRate
        +uint16_t NbrChannels
        +uint16_t BitPerSample
        +uint32_t SubChunk2Size
    }
    class FILELIST_FileTypeDef {
        +FILELIST_LineTypeDef file[24]
        +uint16_t ptr
    }
    class FILELIST_LineTypeDef {
        +uint8_t type
        +uint8_t name[40]
    }
    FILELIST_FileTypeDef "1" o-- "24" FILELIST_LineTypeDef
```

## 29. 全域變數關係

```mermaid
flowchart LR
    Appli["Appli_state<br/>usb_host.c"] --> MainReady["main.c 判斷可不可以開始播放"]
    AudioState["AudioState<br/>waveplayer.c"] --> MainUI["main.c 顯示 LCD / LED"]
    AudioState --> PlayerSM["AUDIO_PLAYER_Process() 狀態機"]
    FileList["FileList<br/>waveplayer.c"] --> FileScan["File_Handling.c 掃到 WAV 後填入"]
    WaveFormat["WaveFormat<br/>waveplayer.c"] --> AudioFreq["AUDIO_OUT_Init 使用 SampleRate"]
    BufferCtl["BufferCtl<br/>waveplayer.c"] --> DMAFlow["DMA half/full callback 和補資料"]
    WavFile["WavFile<br/>waveplayer.c"] --> FatRead["f_open / f_read / f_close"]
    uwVolume["uwVolume<br/>waveplayer.c static"] --> CodecVol["真正 codec 音量"]
    uwVolumeTmp["uwVolume_tmp<br/>main.c"] --> LCDVol["LCD 顯示用音量"]
```

## 30. API 呼叫圖：從 main 往下

```mermaid
flowchart TD
    Main["main()"] --> Init["MX_* 初始化"]
    Main --> USBProc["MX_USB_HOST_Process()"]
    Main --> Mount["Mount_USB()"]
    Main --> Start["AUDIO_PLAYER_Start(0)"]
    Main --> Process["AUDIO_PLAYER_Process(TRUE)"]
    Main --> LCD["lcd16x2_i2c_*"]

    Start --> Count["AUDIO_GetWavObjectNumber()"]
    Count --> Parse["AUDIO_StorageParse()"]
    Parse --> Dir["f_opendir / f_readdir / f_closedir"]
    Start --> Open["f_open / f_read header / f_lseek / f_read buffer"]
    Start --> PlayerInit["PlayerInit(SampleRate)"]
    PlayerInit --> AudioInit["AUDIO_OUT_Init()"]
    Start --> AudioPlay["AUDIO_OUT_Play()"]

    Process --> AudioStop["AUDIO_OUT_Stop()"]
    Process --> AudioPause["AUDIO_OUT_Pause()"]
    Process --> AudioResume["AUDIO_OUT_Resume()"]
    Process --> AudioVolume["AUDIO_OUT_SetVolume()"]
    Process --> StartAgain["AUDIO_PLAYER_Start(FilePos)"]
```

## 31. API 呼叫圖：音訊輸出往下

```mermaid
flowchart TD
    AudioInit["AUDIO_OUT_Init()"] --> Clock["AUDIO_OUT_ClockConfig()"]
    AudioInit --> Msp["AUDIO_OUT_MspInit() 或 HAL_I2S_MspInit()"]
    AudioInit --> I2SInit["I2S3_Init()"]
    AudioInit --> ReadID["cs43l22_drv.ReadID()"]
    AudioInit --> CodecInit["pAudioDrv->Init()"]

    AudioPlay["AUDIO_OUT_Play()"] --> CodecPlay["pAudioDrv->Play()"]
    AudioPlay --> DMATx["HAL_I2S_Transmit_DMA()"]

    AudioPause["AUDIO_OUT_Pause()"] --> CodecPause["pAudioDrv->Pause()"]
    AudioPause --> DMAPause["HAL_I2S_DMAPause()"]

    AudioResume["AUDIO_OUT_Resume()"] --> CodecResume["pAudioDrv->Resume()"]
    AudioResume --> DMAResume["HAL_I2S_DMAResume()"]

    AudioStop["AUDIO_OUT_Stop()"] --> DMAStop["HAL_I2S_DMAStop()"]
    AudioStop --> CodecStop["pAudioDrv->Stop()"]
```

## 32. FatFs 掛載與 driver 連接

```mermaid
flowchart TD
    FatInit["MX_FATFS_Init()"] --> Link["FATFS_LinkDriver(&USBH_Driver, USBHPath)"]
    Link --> Path["USBHPath 得到邏輯磁碟路徑"]
    Mount["Mount_USB()"] --> FMount["f_mount(&USBHFatFS, USBHPath, 1)"]
    FMount --> Ready["之後 f_opendir / f_open / f_read 才有路徑可走"]
```

## 33. USB Host 底層回呼

```mermaid
flowchart LR
    HCD["HAL HCD / USB_OTG_FS"] --> Connect["HAL_HCD_Connect_Callback"]
    Connect --> LLConnect["USBH_LL_Connect"]
    HCD --> Disconnect["HAL_HCD_Disconnect_Callback"]
    Disconnect --> LLDisconnect["USBH_LL_Disconnect"]
    HCD --> PortOn["HAL_HCD_PortEnabled_Callback"]
    PortOn --> LLPortOn["USBH_LL_PortEnabled"]
    HCD --> PortOff["HAL_HCD_PortDisabled_Callback"]
    PortOff --> LLPortOff["USBH_LL_PortDisabled"]
    USBH["USB Host Library"] --> UserProc["USBH_UserProcess"]
    UserProc --> AppState["Appli_state"]
```

## 34. VBUS 控制

```mermaid
flowchart TD
    USBHLL["USBH_LL_DriverVBUS(phost, state)"] --> Platform["MX_DriverVbusFS(state)"]
    Platform --> Check{"state == 0 ?"}
    Check -- "是" --> High["PC0 = GPIO_PIN_SET"]
    Check -- "否" --> Low["PC0 = GPIO_PIN_RESET"]
```

這段程式碼有特別反相處理：`state == 0` 時把 PC0 拉高，`state != 0` 時拉低。看硬體時要照程式碼，不要只用函式名稱猜。

## 35. 播放一首歌時的時間線

```mermaid
sequenceDiagram
    participant USB as USB 隨身碟
    participant Fat as FatFs
    participant Wave as waveplayer.c
    participant DMA as I2S DMA
    participant Codec as CS43L22

    Wave->>Fat: f_read 4096 bytes 到 buffer
    Fat->>USB: USB MSC read sectors
    Wave->>DMA: HAL_I2S_Transmit_DMA(buffer)
    DMA->>Codec: 送前半段 PCM
    DMA-->>Wave: half callback, state=HALF
    Wave->>Fat: 補前半段
    Fat->>USB: 再讀下一段
    DMA->>Codec: 送後半段 PCM
    DMA-->>Wave: full callback, state=FULL
    Wave->>Fat: 補後半段
    Fat->>USB: 再讀下一段
```

## 36. 檔案播完時

```mermaid
flowchart TD
    Playing["AudioState = PLAY"] --> CheckEnd{"BufferCtl.fptr >= WaveFormat.FileSize ?"}
    CheckEnd -- "否" --> Continue["繼續看 half/full buffer 狀態"]
    CheckEnd -- "是" --> StopOut["AUDIO_OUT_Stop(CODEC_PDWN_SW)"]
    StopOut --> SetNext["AudioState = AUDIO_STATE_NEXT"]
    SetNext --> NextCase["下一次進 NEXT case"]
    NextCase --> Inc["FilePos++"]
    Inc --> Over{"FilePos >= WAV 數量 ?"}
    Over -- "否" --> StartNext["AUDIO_PLAYER_Start(FilePos)"]
    Over -- "是且 isLoop=true" --> Loop0["FilePos = 0<br/>AUDIO_PLAYER_Start(0)"]
    Over -- "是且 isLoop=false" --> StopState["AudioState = STOP"]
```

## 37. 目前程式比較容易踩到的點

```mermaid
flowchart TD
    A["專案名叫 USB_MP3"] --> B["程式碼掃 wav/WAV<br/>沒有 MP3 decoder"]
    C["UART checksum 累加被註解"] --> D["sum 固定 0<br/>封包尾 byte 要是 0 才會有效"]
    E["uwVolume 與 uwVolume_tmp 分開"] --> F["codec 真音量在 waveplayer.c<br/>LCD 顯示值在 main.c"]
    G["AUDIO_GetWavObjectNumber() 沒有失敗 return"] --> H["如果 AUDIO_StorageParse() 失敗<br/>回傳值未明確定義"]
    I["I2S DMA 設定分散"] --> J["main 的 MSP 用 DMA1 Stream5<br/>AUDIO.h 另外有 Stream7 巨集<br/>讀中斷時要看實際 it.c"]
```

這些點不是問題清單，而是讀程式時很容易看歪的地方。先抓住這幾點，後面比較不會繞路。

## 38. 除錯時先看哪裡

```mermaid
flowchart TD
    NoSound["沒有聲音"] --> USBReady{"Appli_state 有到 READY ?"}
    USBReady -- "否" --> CheckUSB["看 USBH_UserProcess / OTG_FS_IRQHandler / VBUS PC0"]
    USBReady -- "是" --> HasWav{"AUDIO_GetWavObjectNumber() > 0 ?"}
    HasWav -- "否" --> CheckFile["確認隨身碟根目錄有 wav/WAV 檔"]
    HasWav -- "是" --> Started{"AUDIO_PLAYER_Start() 回傳 OK ?"}
    Started -- "否" --> CheckHeader["看 f_open / f_read / WaveFormat.SampleRate"]
    Started -- "是" --> DMAIrq{"DMA half/full callback 有跑 ?"}
    DMAIrq -- "否" --> CheckDMA["看 DMA1_Stream5_IRQHandler / HAL_I2S callbacks"]
    DMAIrq -- "是" --> CodecOK{"codec ReadID / Init OK ?"}
    CodecOK -- "否" --> CheckI2C1["看 I2C1 PB6/PB9 / CS43L22 reset PD4"]
    CodecOK -- "是" --> CheckHW["看 I2S 腳位與喇叭/耳機硬體"]
```

## 39. 控制訊號與音訊資料分開看

```mermaid
flowchart LR
    subgraph Control["控制訊號"]
        BLE["藍牙 UART3"] --> AudioStateA["AudioState"]
        Button["PA0 按鍵"] --> AudioStateA
        MainUI["main.c LCD/LED"] --> Display["使用者看得到的狀態"]
    end

    subgraph Data["音訊資料"]
        USBFile["USB WAV file"] --> FatRead["f_read"]
        FatRead --> Buffer["BufferCtl.buff"]
        Buffer --> DMA["I2S DMA"]
        DMA --> Codec["CS43L22"]
        Codec --> Sound["聲音"]
    end

    AudioStateA --> Player["AUDIO_PLAYER_Process()"]
    Player --> FatRead
    Player --> DMA
    Player --> Codec
```

## 40. 5 分鐘講解這個專案

這段整理成五分鐘版本，重點放在流程和資料怎麼走，不需要一開始就背所有函式。

### 第 0:00 到 0:40，先講總圖

用第 01 張圖開場：

> 整體可以看成 USB WAV 播放器。USB 隨身碟提供檔案，FatFs 負責檔案系統，`waveplayer.c` 把 WAV 讀進 buffer，`AUDIO.c` 用 I2S DMA 丟給 CS43L22 codec。藍牙和按鍵的角色是改 `AudioState`，播放動作由狀態機接手處理。

### 第 0:40 到 1:30，講啟動順序

看第 04、05、06 張圖：

> 上電後 `main()` 先初始化 GPIO、DMA、I2C、I2S、FatFs、USB Host、UART、LCD。進入 while loop 後，每圈都跑 `MX_USB_HOST_Process()`。USB Host callback 把 `Appli_state` 改成 `APPLICATION_READY` 之後，主程式才掛載 USB、啟動第 0 首。

### 第 1:30 到 2:40，講音訊資料怎麼流

看第 07、10、11、12、35 張圖：

> 播放時不會把整首歌一次塞進 RAM。程式主要靠一個 4096 bytes buffer 輪流補資料。DMA 送前半段時，CPU 等 callback 標記前半段送完，再把前半段補成新資料；後半段也是同樣做法。資料路線是 `f_read()`、FatFs、USB MSC、buffer、I2S DMA、CS43L22、喇叭。

### 第 2:40 到 3:35，講控制怎麼進來

看第 13、18、19、20 張圖：

> 藍牙 UART 收到封包後，callback 主要把 command 轉成 `AudioState`，例如 0x00 是下一首、0x04 是暫停。PA0 按鍵也是同一個概念，播放中按下去就把狀態改成下一首。切歌、暫停、調音量都集中在 `AUDIO_PLAYER_Process()` 裡處理。

### 第 3:35 到 4:25，講 codec 和周邊

看第 22、23、24、25、26、31 張圖：

> I2C1 是 codec 控制，寫 CS43L22 暫存器；I2C3 是 LCD；I2S3 是音訊資料。UART3 接藍牙，USB OTG FS 接隨身碟。中斷進來後大多先進 HAL handler，再回到我們自己的 callback。

### 第 4:25 到 5:00，補接手注意事項

看第 37、38、39 張圖：

> 專案名稱是 `USB_MP3`，但目前程式碼走的是 WAV 播放流程。藍牙 checksum 目前 `sum` 沒累加，所以封包尾端要配合現況。音量有兩份變數，真正送 codec 的在 `waveplayer.c`，LCD 顯示的是 `main.c` 的暫存值。除錯可以照第 38 張圖：先看 USB ready，再看 WAV 清單，再看 DMA callback，最後看 codec 和硬體。

五分鐘內抓住三件事就夠了：程式從哪裡開始跑、音訊資料怎麼不斷被補進 DMA、控制命令最後怎麼改變播放器狀態。
