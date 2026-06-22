# STM32F407 FreeRTOS USB Audio 流程圖

韌體從 USB Mass Storage 讀取 Stereo 16-bit PCM WAV，FatFs 負責檔案存取，
I2S3 DMA 負責連續輸出，CS43L22 負責數位音訊轉換與類比輸出。

圖中的流程依目前原始碼整理，用來對照 Task、Queue、ISR 與狀態轉換；不代表
板端時序、USB 延遲或音訊波形已完成量測。

## 圖例

| 標記 | 代表內容 |
|---|---|
| `[HW]` | 板端硬體（Hardware） |
| `[ISR]` | 中斷服務函式（Interrupt Service Routine） |
| `[Task]` | FreeRTOS Task |
| `[Queue]` | FreeRTOS Queue |
| `[Notify]` | Direct Task Notification |
| `[State]` | 共用狀態或 Event Group |
| `[Diag]` | Diagnostics 與 Watchdog |

---

## 1. 系統資料流

```mermaid
flowchart LR
    USB["[HW] USB 隨身碟"] --> OTG["USB OTG FS Host"]
    OTG --> MSC["USB MSC"]
    MSC --> FAT["FatFs"]
    FAT --> FILE["[Task] FileTask"]
    FILE --> BUF["Audio DMA Buffer<br/>4096 bytes"]
    BUF --> DMA["I2S3 + DMA1 Stream5"]
    DMA --> CODEC["[HW] CS43L22"]
    CODEC --> OUT["Headphone / Speaker"]

    TERM["[HW] UART Terminal / HC-05"] --> CLI["[Task] CliTask"]
    BTN["[HW] PA0 Button"] --> EXTI["[ISR] EXTI0"]
    CLI --> CMDQ["[Queue] SystemCmd"]
    EXTI --> CMDQ
    CMDQ --> CTRL["[Task] ControlTask"]
    CTRL --> AUDIOQ["[Queue] AudioCmd"]
    AUDIOQ --> AUDIO["[Task] AudioTask"]
    AUDIO --> FILEQ["[Queue] AudioFile"]
    FILEQ --> FILE

    DMA -->|"Half / Complete"| DMAQ["[Queue] AudioDma"]
    DMAQ --> AUDIO
    DMA -->|"Wake Notify"| AUDIO
    AUDIO --> STATE["[State] Audio Context"]
    FILE --> STATE
    UI["[Task] UiTask"] --> STATE
    MON["[Task] MonitorTask"] --> DIAG["[Diag] Counters / Heartbeat"]
    DIAG --> IWDG["[HW] IWDG"]
```

---

## 2. Task Priority 與責任

```mermaid
flowchart TB
    P5["Priority 5<br/>AudioTask<br/>DMA Event / Playback State"]
    P4["Priority 4<br/>FileTask<br/>USB / FatFs / Refill"]
    P3A["Priority 3<br/>ControlTask<br/>Command Routing"]
    P3B["Priority 3<br/>CliTask<br/>UART / Parser"]
    P2["Priority 2<br/>MonitorTask<br/>Health / Watchdog"]
    P1["Priority 1<br/>UiTask<br/>LCD"]

    P5 --> P4
    P4 --> P3A
    P4 --> P3B
    P3A --> P2
    P3B --> P2
    P2 --> P1
```

AudioTask 的 priority 高於其他 application tasks。FileTask priority 次之，
目的是讓 DMA boundary 與 FatFs refill 優先於控制、監控與顯示。LCD 更新
週期設定為 500 ms。

---

## 3. 開機流程

```mermaid
sequenceDiagram
    participant Reset as MCU Reset
    participant Main as main()
    participant HAL as STM32 HAL
    participant App as app_main_start()
    participant RTOS as FreeRTOS Scheduler
    participant Tasks as Application Tasks

    Reset->>Main: Reset_Handler
    Main->>HAL: HAL_Init()
    Main->>HAL: SystemClock_Config()
    Main->>HAL: GPIO / DMA / I2C / I2S / USB / UART Init
    Main->>App: app_main_start()
    App->>Tasks: Create Queue / Mutex / Event Group
    App->>Tasks: Create 6 Tasks
    App->>RTOS: vTaskStartScheduler()
    RTOS->>Tasks: Start preemptive scheduling
```

若 object 或 task 建立失敗，系統進入 safe error loop：關閉中斷、關閉
PD12～PD14、點亮 PD15。

---

## 4. USB 插入與開始播放

```mermaid
sequenceDiagram
    participant USB as USB Device
    participant HCD as USB HCD ISR
    participant Host as USB Host State Machine
    participant File as FileTask
    participant FatFs
    participant Audio as AudioTask
    participant Codec as CS43L22 / I2S DMA

    USB->>HCD: Connect
    HCD->>Host: Device connected
    File->>Host: MX_USB_HOST_Process()
    Host-->>File: APPLICATION_READY
    File->>FatFs: f_mount()
    File->>FatFs: Scan root directory
    File->>FatFs: f_open() + Parse WAV
    File->>FatFs: Read initial 4096 bytes
    File->>Audio: FILE_READY notification
    Audio->>Codec: Configure PLLI2S / I2S / Codec
    Audio->>Codec: HAL_I2S_Transmit_DMA()
```

---

## 5. Command Path

```mermaid
sequenceDiagram
    participant Port as USART3 / PA0
    participant CLI as CliTask or EXTI ISR
    participant Q1 as SystemCmd Queue
    participant Control as ControlTask
    participant Q2 as AudioCmd Queue
    participant Audio as AudioTask
    participant File as FileTask

    Port->>CLI: Input event
    CLI->>Q1: SystemCommand
    Q1->>Control: Receive command
    Control->>Q2: Forward command
    Q2->>Audio: Execute command

    alt Play / Pause / Stop
        Audio->>Audio: Update playback state
    else Next / Prev
        Audio->>File: Prepare selected WAV
    else Volume
        Audio->>Audio: Update codec volume
    end
```

ControlTask 不直接呼叫 codec 或 FatFs。它只負責把不同輸入來源轉成一致的
command flow。

---

## 6. UART Receive-to-Idle DMA

```mermaid
sequenceDiagram
    participant UART as USART3 + DMA
    participant ISR as HAL_UARTEx_RxEventCallback
    participant Ring as Software Ring Buffer
    participant CLI as CliTask
    participant Parser as Command Dispatch Table

    UART->>ISR: IDLE or DMA event
    ISR->>Ring: Copy received bytes
    ISR->>UART: Re-arm RX DMA
    ISR->>CLI: vTaskNotifyGiveFromISR()
    CLI->>Ring: Pop bytes
    CLI->>CLI: Assemble one line
    CLI->>Parser: Tokenize + Dispatch
```

UART Error Callback：

```mermaid
flowchart LR
    ERR["UART Error ISR"] --> COUNT["Record Error Counter"]
    COUNT --> FLAG["Set Restart Flag"]
    FLAG --> NOTIFY["Notify CliTask"]
    NOTIFY --> ABORT["CliTask: HAL_UART_AbortReceive"]
    ABORT --> RESTART["Restart Receive-to-Idle DMA"]
```

Abort 與 DMA restart 在 CliTask 執行，不在 ISR 內進行。

---

## 7. DMA Ping-pong Buffer

```text
audio_dma_buffer[4096]
┌──────────────────────────────┐
│ Half 0：2048 bytes           │
├──────────────────────────────┤
│ Half 1：2048 bytes           │
└──────────────────────────────┘
```

```mermaid
sequenceDiagram
    participant DMA as I2S DMA
    participant ISR as DMA ISR
    participant DmaQueue as AudioDma Queue
    participant Audio as AudioTask
    participant Queue as AudioFile Queue
    participant File as FileTask
    participant FatFs

    DMA->>ISR: Half Complete
    ISR->>DmaQueue: Enqueue DMA_HALF
    ISR->>Audio: Wake notification
    Audio->>DmaQueue: Receive DMA_HALF
    Audio->>Audio: Verify Half 1 ready
    Audio->>Queue: Refill Half 0
    Queue->>File: REFILL request
    File->>FatFs: f_read(2048)
    File->>File: Validate session + generation
    File->>DMA: Check NDTR safety margin
    File->>DMA: Commit Half 0

    DMA->>ISR: Complete
    ISR->>DmaQueue: Enqueue DMA_COMPLETE
    ISR->>Audio: Wake notification
    Audio->>DmaQueue: Receive DMA_COMPLETE
    Audio->>Audio: Verify Half 0 ready
    Audio->>Queue: Refill Half 1
```

44.1 kHz、16-bit Stereo：

```text
Bytes per frame = 2 channels × 2 bytes = 4 bytes
Frames per half = 2048 / 4 = 512 frames
Half-buffer time = 512 / 44100 ≈ 11.6 ms
```

---

## 8. Underrun 處理

```mermaid
flowchart TD
    A["DMA 進入下一個 Half"] --> B{"ready_generation == half_generation？"}
    B -->|Yes| C["使用新音訊資料"]
    B -->|No| D["Audio Underrun +1"]
    D --> E["Increment Generation"]
    E --> F["Fill Silence"]
    F --> G["Reject Late File Read Commit"]
    G --> H["繼續播放"]
```

Generation 用來拒絕逾時的 `f_read()` commit；`NDTR` 安全餘量檢查用來
阻止 FileTask 在 DMA 即將切換 half-buffer 時開始 commit。

---

## 9. 切歌與 Stream Session

```mermaid
sequenceDiagram
    participant Audio as AudioTask
    participant File as FileTask
    participant Old as Old f_read()
    participant New as New Track

    Audio->>Audio: Stop DMA
    Audio->>Audio: stream_session++
    Audio->>File: PREPARE new session
    Old-->>File: Old read completes late
    File->>File: Compare request.session_id
    File-->>Old: Reject stale commit
    File->>New: Open and fill new buffer
```

Stop、Next、Prev 與 USB disconnect 都會更新 session。舊 session 不能修改：

- DMA buffer
- Data remaining
- EOF state
- Ready generation

---

## 10. USB 拔除與恢復

```mermaid
stateDiagram-v2
    [*] --> USB_WAIT
    USB_WAIT --> FILE_READY: Mount + Parse OK
    FILE_READY --> PLAYING: DMA Start
    PLAYING --> USB_WAIT: USB Disconnect
    PAUSED --> USB_WAIT: USB Disconnect
    USB_WAIT --> FILE_READY: USB Reinsert
    FILE_READY --> ERROR: WAV / File Error
    USB_WAIT --> ERROR: Mount Error
```

拔除 USB 時：

1. FileTask 關閉檔案。
2. Unmount FatFs。
3. 清空 AudioFile Queue。
4. 清除檔案清單。
5. AudioTask 停止 I2S DMA。
6. 狀態回到 `USB_WAIT`。

---

## 11. Diagnostics 與 Watchdog

```mermaid
flowchart TD
    MON["MonitorTask 每 1 秒"] --> HEAP["Free Heap / Minimum Heap"]
    MON --> STACK["Task Stack High Water Mark"]
    MON --> HB["Task Heartbeat Age"]
    MON --> QUEUE["Queue Usage"]
    MON --> ERR["Error Counters"]
    HEAP --> HEALTH{"Core Tasks Healthy？"}
    STACK --> HEALTH
    HB --> HEALTH
    QUEUE --> HEALTH
    ERR --> HEALTH
    HEALTH -->|Yes| FEED["Refresh IWDG"]
    HEALTH -->|No| SKIP["Skip Refresh"]
    SKIP --> RESET["IWDG Timeout / MCU Reset"]
```

各 Task 不自行 refresh IWDG。MonitorTask 依 heartbeat 與 fatal state 決定
是否 refresh；實際 reset 時間仍受 LSI 誤差影響。

---

## 12. Error Path

```mermaid
flowchart LR
    STACK["Stack Overflow Hook"] --> SAFE["Safe Error Loop"]
    MALLOC["Malloc Failed Hook"] --> SAFE
    ASSERT["configASSERT"] --> SAFE
    SAFE --> IRQ["Disable Interrupts"]
    IRQ --> LED["PD15 ON<br/>PD12-14 OFF"]
    LED --> HOLD["Hold CPU"]
```

一般可恢復錯誤使用 diagnostics counter；Stack Overflow、Malloc Failed 與
Assert 屬於 fatal error，進入固定錯誤狀態。

---

## 13. RTOS Objects 對照

| Object | Producer | Consumer | 資料 |
|---|---|---|---|
| SystemCmd Queue | CliTask、EXTI ISR | ControlTask | `SystemCommand` |
| AudioCmd Queue | ControlTask | AudioTask | `SystemCommand` |
| AudioFile Queue | AudioTask | FileTask | Prepare / Refill / Close |
| AudioDma Queue | I2S DMA ISR | AudioTask | Half / Complete / Error |
| Task Notification | I2S DMA ISR | AudioTask | Wake-up |
| Task Notification | UART ISR | CliTask | RX data ready |
| Mutex | AudioTask、FileTask | CLI、UI、Monitor | Audio context |
| Event Group | AudioTask、FileTask | System modules | USB / File / Play state |

---

## 14. 設計要點

1. DMA ISR 只送 queue event 與 notification，不執行 FatFs、LCD 或 UART output。
2. FileTask 是 FatFs 操作的單一 owner。
3. AudioTask 是 codec 與播放狀態的單一 owner。
4. Queue 保留 command 與 DMA event；Task Notification 負責快速喚醒 Task。
5. Generation 偵測 half-buffer deadline miss。
6. Stream Session 阻擋跨歌曲的 stale read。
7. MonitorTask 統一判斷 health 並 refresh Watchdog。
8. CLI 提供 Heap、Stack、Queue、Buffer 與 Error Counter。
