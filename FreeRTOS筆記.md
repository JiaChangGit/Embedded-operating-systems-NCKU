# FreeRTOS 筆記

---

## 1. Button Debounce

### Hardware Debounce
- 使用電容器
- 使用 RC 電路

### Software Debounce
- 延遲計數
- 狀態機
- 中斷觸發

---

## 2. HAL (Hardware Abstraction Layer) Library

### HAL_GPIO_ReadPin()

```c
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
```

| 參數 | 說明 |
|------|------|
| `GPIOx` | 指向 `GPIO_TypeDef` 型別的 pointer，表示要讀取的 GPIO 端口。例如讀取 PA0 時設為 `GPIOA`。 |
| `GPIO_Pin` | 表示要讀取的 GPIO 引腳的 bit mask。例如讀取 PA0 時設為 `GPIO_PIN_0`。 |
| Return Value | 回傳 `GPIO_PinState` 型別（enum）：`GPIO_PIN_RESET`（低電位，0）或 `GPIO_PIN_SET`（高電位，1）。 |

**GPIO 端口與 Pin 對應關係：**

```
GPIOA
 ├─ PA0
 ├─ PA1
 ├─ PA2
 ...
 └─ PA15
```

**Pin Mask 定義：**

```c
#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
...
#define GPIO_PIN_15  ((uint16_t)0x8000)
```

---

### HAL_UART_Transmit()

使用輪詢（Polling）方式透過 UART 傳送資料。

```c
HAL_UART_Transmit(UART_HandleTypeDef *huart,
                  uint8_t *pTxData,
                  uint16_t Size,
                  uint32_t Timeout);
```

| 參數 | 說明 |
|------|------|
| `huart` | UART 控制結構（Handle）的指標 |
| `pTxData` | 欲傳送資料緩衝區（Buffer）的起始位址 |
| `Size` | 欲傳送的資料長度（Byte 數量） |
| `Timeout` | 等待 UART 傳送完成的最長時間（毫秒，ms） |

---

### itoa()

將整數轉換為 char array，需引入 `#include <stdlib.h>`。

```c
char *itoa(int value, char *str, int base);
```

| 參數 | 說明 |
|------|------|
| `value` | 要轉換的整數值 |
| `str` | 指向儲存結果的字元陣列（string）的 pointer |
| `base` | 表示進制的整數值，例如 10 表示十進制，16 表示十六進制 |
| Return Value | 回傳指向結果字串的 pointer |

---

## 3. Debounce 實作

### 方法一：計時 + 放開確認法

**流程：**

```
按下按鈕
   │
   ▼
記錄按下時間
   │
   ▼
等待超過 debounceDelay
   │
   ▼
確認按鈕已放開
   │
   ▼
算一次有效按鍵
   │
   ▼
切換 LED 狀態
```

**Button Task 詳細流程 (`vButtonHandler`)：**

```
while(1)
   │
   ▼
讀取按鈕狀態 HAL_GPIO_ReadPin()
   │
   ▼
按鈕是否被按下？ buttonState == GPIO_PIN_SET
   │
   ├── Yes ──► 記錄目前 tick
   │            lastDebounceTime = xTaskGetTickCount()
   │            debounceInProgress = pdTRUE
   │
   ▼
是否正在 debounce？ debounceInProgress == pdTRUE
   │
   ▼
是否超過 50 tick？ xTaskGetTickCount() - lastDebounceTime > debounceDelay
   │
   ├── No  ──► vTaskDelay(10) ──► 下一輪
   │
   └── Yes
         │
         ▼
    按鈕是否已放開？ buttonState != GPIO_PIN_SET
         │
         ├── No  ──► vTaskDelay(10) ──► 下一輪
         │
         └── Yes
               │
               ▼
          state ^= 1  (LED 狀態反轉)
               │
               ▼
          xQueueSend(xQueue, &state, 1)  (把 LED 狀態送給其他 task)
               │
               ▼
          debounceInProgress = pdFALSE
               │
               ▼
          vTaskDelay(10)
```

**程式碼：**

```c
void vButtonHandler(void *pvParameters) {
    unsigned long lastDebounceTime = 0;
    unsigned long debounceDelay = 50;
    static unsigned int state = 0;             // LED 燈狀態
    BaseType_t debounceInProgress = pdFALSE;   // 記錄是否正在 debounce

    while (1) {
        // 取得 button 目前的狀態
        int buttonState = HAL_GPIO_ReadPin(Blue_Button_GPIO_Port, GPIO_PIN_0);

        // 若 button 狀態是 GPIO_PIN_SET，就代表 button 被按下
        if (buttonState == GPIO_PIN_SET) {
            // 記錄目前的時間
            lastDebounceTime = xTaskGetTickCount();
            // 開始進入 debounce 的過程
            debounceInProgress = pdTRUE;
        }

        // 若是在 debounce 的狀態，且已超過 debounceDelay
        if (debounceInProgress && xTaskGetTickCount() - lastDebounceTime > debounceDelay) {

            // 若 button 狀態不是 GPIO_PIN_SET，也就是 button 已經被放開
            // 這就代表 button 已經被按了一次，且被放開
            if (buttonState != GPIO_PIN_SET) {
                // 更改 LED 燈的狀態
                state ^= 1;
                // 把 LED 燈的狀態送到 msgQueue
                xQueueSend(xQueue, (int *) &state, 1);
                // debounce 結束
                debounceInProgress = pdFALSE;
            }
        }
        vTaskDelay(10);
    }
}
```

---

### 方法二：移位暫存歷史狀態法

**概念：** 連續觀察按鈕 N 次，把歷史狀態存進一個變數，當歷史記錄符合特定 Pattern 時，認定按鍵穩定。

**流程：**

```
每 10ms 執行一次
       │
       ▼
讀取 GPIO → HAL_GPIO_ReadPin()
       │
       ▼
buttonState 左移 1 bit
       │
       ▼
塞入最新按鍵狀態
       │
       ▼
形成 16-bit 歷史記錄
       │
       ▼
是否等於 0xFF00？  ── 即 1111 1111 0000 0000
                      ^^^^^^^^ 舊資料（8 次高電位）
                               |||||||  連續 8 次穩定狀態（Debounce Time ≈ 80ms）
       │
       ├── No
       │
       └── Yes
             │
             ▼
        按鍵確認成立
             │
             ▼
        LEDstate ^= 1
             │
             ▼
        xQueueSend()
```

**程式碼：**

```c
void vButtonHandler(void *pvParameters) {
    static uint16_t buttonState = 0;
    // 用 16 個 bit 來記錄按鈕的歷史狀態

    static unsigned int LEDstate = 0;

    while (1) {
        buttonState = (buttonState << 1) |
                      HAL_GPIO_ReadPin(Blue_Button_GPIO_Port, GPIO_PIN_0) |
                      0xFE00;

        if (buttonState == 0xFF00) {
            // 按鈕穩定了，可以更新 LED 燈的 state 了
            LEDstate ^= 1;
            xQueueSend(xQueue, (int *) &LEDstate, 1);
        }

        vTaskDelay(10);
    }
}
```

---

## 4. vTaskDelay vs xQueueReceive 設計比較

### ❌ 錯誤設計：用 `vTaskDelay(2000)` 控制亮燈時間

```
LED 亮起
   │
   ▼
vTaskDelay(2000)
   │
   │  ← 這 2 秒內 Task 不處理任何新訊息
   ▼
2 秒後才醒來
   │
   ▼
檢查 Queue 裡的新狀態
```

### ✅ 正確設計：用 `xQueueReceive(..., 2000)`

最多等待 2000 ms，但如果 Queue 提早收到訊息，立刻醒來處理。

```
LED 亮起
   │
   ▼
xQueueReceive(xQueue, &receivedMsg, 2000)
   │
   ├── 2 秒內沒有新按鈕事件
   │       │
   │       ▼
   │   Timeout，自動關燈或維持原邏輯
   │
   └── 2 秒內有新按鈕事件
           │
           ▼
        立刻醒來
           │
           ▼
        馬上切換 LED 狀態
```

---

## 5. FreeRTOS Circular Doubly Linked List

### List_t 結構

```
                 List_t
                     │
                     ▼
┌─────────────────────────────┐
│ uxNumberOfItems = 3         │
│ // 目前 List 裡面有多少節點  │
│ pxIndex  // Round Robin use │
│ xListEnd                    │
└─────────────────────────────┘
          │
          ▼
     Sentinel Node (xListEnd)
          ▲
          │
┌─────────┴──────────┐
│                    │
▼                    │
+---------+      +---------+      +---------+
| Item A  | <--> | Item B  | <--> | Item C  |
+---------+      +---------+      +---------+
      ▲                               │
      └───────────────────────────────┘
```

```c
typedef struct xLIST
{
    listFIRST_LIST_INTEGRITY_CHECK_VALUE
    volatile UBaseType_t uxNumberOfItems;
    ListItem_t           *pxIndex;
    MiniListItem_t        xListEnd;
    listSECOND_LIST_INTEGRITY_CHECK_VALUE
} List_t;
```

### ListItem_t 結構

```c
typedef struct xLIST_ITEM
{
    TickType_t           xItemValue;
    struct xLIST_ITEM   *pxNext;
    struct xLIST_ITEM   *pxPrevious;
    void                *pvOwner;
    struct xLIST        *pxContainer;
} ListItem_t;
```

| 欄位 | 說明 |
|------|------|
| `xItemValue` | 用於排序的值（如 Wake Tick） |
| `pxNext` | 下一個節點 |
| `pxPrevious` | 上一個節點 |
| `pvOwner` | 指向擁有這個 ListItem 的物件（如 TCB） |
| `pxContainer` | 指回所屬的 List |

### TCB 與 ListItem 的關係

```
ListItem_t
     │
     │ pvOwner
     ▼
   TCB_t
+--------------------+
|      TCB_t         |
+--------------------+
| Priority           |
| Stack              |
| State              |
+--------------------+
| xStateListItem     |
+--------------------+
| xEventListItem     |
+--------------------+
```

FreeRTOS Scheduler 使用 Circular Doubly Linked List 管理 Task。每個 Task 的 TCB 內嵌 `ListItem_t`，透過 `pvOwner` 指回 TCB 本身。Ready Task 依優先權放在 `pxReadyTasksLists` 陣列中，每個 Priority 對應一條 Linked List；Delay Task 則放在 `pxDelayedTaskList`，並以 `xItemValue` 儲存 Wake Tick 作排序。`List_t` 使用 Sentinel Node（`xListEnd`）實作環狀雙向鏈結串列，因此能在 O(1) 時間完成 Task 插入、移除以及 Round Robin 排程。

---

## 6. TaskMonitor 實作

### 流程

```
TaskMonitor()
   │
   ▼
vTaskSuspendAll()  ← 暫停 Scheduler，避免 List 被修改
   │
   ▼
印出表格標題
   │
   ▼
走訪 pxReadyTasksLists[]
   │
   ▼
走訪 pxDelayedTaskList
   │
   ▼
走訪 pxOverflowDelayedTaskList
   │
   ▼
xTaskResumeAll()  ← 恢復 Scheduler
```

> **為什麼要 `vTaskSuspendAll()`？**
> `vTaskSuspendAll()` 不會關中斷，但能防止 Scheduler 修改正在走訪的 `pxReadyTasksLists`、`pxDelayedTaskList`、`pxOverflowDelayedTaskList`。

### TCB 欄位說明

```c
tcb->uxBasePriority  // Task 原本的優先權
tcb->uxPriority      // 目前實際優先權（FreeRTOS 有 Priority Inheritance）
```

### Task Stack 記憶體佈局

```
  High Address
+------------------+
|                  |
| unused stack     |
|                  |
+------------------+
| saved context    | <- pxTopOfStack
+------------------+
|                  |
| stack memory     |
+------------------+ <- pxStack
  Low Address
```

- `pxStack`：整塊 stack 的起點
- `pxTopOfStack`：目前 stack pointer 對應的位置

### 常用巨集

```c
#define listGET_ITEM_OF_HEAD_ENTRY(pxList) ((&((pxList)->xListEnd))->pxNext)
// 用途：取得 List 第一個節點，用於遍歷整個 List
// curNode = curNode->pxNext;

ListItem_t *curNode = listGET_ITEM_OF_HEAD_ENTRY(&pxReadyTasksLists[priority]);
// 取得指定 Priority Ready List 的第一個 ListItem

listGET_OWNER_OF_HEAD_ENTRY()
// 用途：拿第一個 Task 的 TCB
pxCurrentTCB = listGET_OWNER_OF_HEAD_ENTRY(pxReadyTasksList + uxTopPriority);
```

FreeRTOS 的 Ready Queue 由 `pxReadyTasksLists[]` 組成，每個 Priority 對應一條 Circular Doubly Linked List。`listGET_ITEM_OF_HEAD_ENTRY()` 會透過 `xListEnd.pxNext` 取得第一個真正的節點；之後可透過 `curNode->pxNext` 依序遍歷整條 List，而 `curNode->pvOwner` 則可取得對應 Task 的 TCB。

### 注意事項

- FreeRTOS List 是環狀雙向鏈結串列，不能用 `NULL` 作為結尾
- 不建議在 `vTaskSuspendAll()` 期間做長時間 blocking UART 傳輸；較好的做法是先快速複製資料，恢復 Scheduler 後再輸出

---

## 7. vListInsertEnd() — 尾端插入函式

FreeRTOS 採用 Circular Doubly Linked List，並使用 `xListEnd` 作為 Sentinel Node。插入時不會放在 `xListEnd` 後面，而是插在 `xListEnd` 前面，因此形成「尾端插入」。

**插入流程：**

1. 新節點的 `pxNext` 指向 `xListEnd`
2. 新節點的 `pxPrevious` 指向目前最後一個節點
3. 原本最後一個節點的 `pxNext` 指向新節點
4. `xListEnd` 的 `pxPrevious` 指向新節點
5. 設定 `pxContainer` 指回所屬 List
6. `uxNumberOfItems++`

由於是雙向環狀鏈結串列，整個插入操作時間複雜度為 **O(1)**。

---

## 8. Semaphore API

### 建立 Binary Semaphore

```c
SemaphoreHandle_t xSemaphoreCreateBinary(void);
// 建立成功回傳 semaphore handle；失敗回傳 NULL
```

### xSemaphoreTake()

```c
BaseType_t xSemaphoreTake(
    SemaphoreHandle_t xSemaphore,  // 要等待哪一個 semaphore
    TickType_t        xTicksToWait // 最多等待多久
);
// 成功拿到 semaphore：pdTRUE；等太久 timeout：pdFALSE
```

### xSemaphoreGiveFromISR()

```c
BaseType_t xSemaphoreGiveFromISR(
    SemaphoreHandle_t  xSemaphore,
    BaseType_t        *pxHigherPriorityTaskWoken
    // 若叫醒了更高優先權的 Task，ISR 結束時應立刻切換過去執行
);
```

### UART ISR + Semaphore 典型流程

```
UART 收到資料
    │
    ▼
觸發 UART ISR
    │
    ▼
xSemaphoreGiveFromISR()
    │
    ▼
叫醒正在等待的 UartTask
    │
    ▼
xSemaphoreTake() 回傳 pdTRUE
    │
    ▼
Task 開始處理 UART 資料
```

---

## 9. vHandlerTask 實作範例

```c
void vHandlerTask(void *pvParameters)
{
    for (;;)
    {
        /* Take the semaphore */
        if (xSemaphoreTake(xSemaphore, LONG_TIME) == pdTRUE) {
            // semaphore was obtained
            // Orange LED blinks 5 times
            for (int i = 0; i < 10; ++i) {
                HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
                uint32_t beginTime = HAL_GetTick();
                while (HAL_GetTick() - beginTime < 500 / portTICK_RATE_MS) {}
            }

            // Reset interrupt register
            // 只要去讀這個 register，就會恢復到發出 interrupt 之前的狀態
            MEMS_Read(LIS3DSH_OUTS1_ADDR, &data);
        }
    }
}
```

**執行流程：**

```
vHandlerTask 一直等待 semaphore
    ↓
ISR 或其他地方 give semaphore
    ↓
xSemaphoreTake() 成功
    ↓
橘色 LED 閃 5 次
    ↓
讀 LIS3DSH_OUTS1_ADDR 清除 interrupt 狀態
```

> **重要：** Reset interrupt register 必須在 `vHandlerTask` 最後執行，因為 LED 閃爍期間若再次晃動開發板，不應觸發新的 interrupt。需等橘色 LED 閃爍五次結束後才 reset interrupt register。

---

## 10. main() 初始化流程

```c
int main(void)
{
    /* USER CODE BEGIN 2 */
    xSemaphore = xSemaphoreCreateBinary();
    WAKEUP_STATE_MACHINE_CONFIG

    xTaskCreate(Green_LED_Task, "Green LED",   1000, NULL, 1, NULL);
    xTaskCreate(vHandlerTask,   "Handler Task", 1000, NULL, 4, NULL);

    vTaskStartScheduler();
    /* USER CODE END 2 */
}
```

**流程圖：**

```
建立 Semaphore
    ↓
設定硬體（按鈕 / 加速度計中斷）
    ↓
建立兩個 Task
    ↓
啟動 FreeRTOS Scheduler
    ↓
開始多工執行

           +-------------------+
           |       main()      |
           +---------+---------+
                     │
                     ▼
      xSemaphoreCreateBinary()
                     │
                     ▼
       WAKEUP_STATE_MACHINE_CONFIG
                     │
                     ▼
      +--------------+---------------+
      │                              │
      ▼                              ▼
Green_LED_Task                 vHandlerTask
(priority=1)                  (priority=4)
      │                              │
      +──────────等待中斷─────────────+
                                ^
                                │
                      EXTI Interrupt
                                │
                   xSemaphoreGiveFromISR()
```

> **注意：** 建立 Task 時，`vHandlerTask` 的 priority 要比 `Green_LED_Task` 還要高。Priority 範圍是 `0 ~ (configMAX_PRIORITIES - 1)`。

---

## 11. FreeRTOS Heap 管理（heap_2.c）

### BlockLink_t 結構

```c
// free block 的 metadata，FreeRTOS allocator 用來管理 heap 的資料
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK *pxNextFreeBlock;  // 指向下一塊 free block
    size_t               xBlockSize;       // 記錄這個 block 的總大小
} BlockLink_t;
```

**記憶體佈局：**

```
free block 起始位址
    │
    ▼
+--------------------------+
| BlockLink_t header       |
| - pxNextFreeBlock        |
| - xBlockSize             |
+--------------------------+
| 可分配出去的 memory       |
|                          |
+--------------------------+
```

### Alignment 計算

```c
// 把 sizeof(BlockLink_t) 向上補齊到 alignment 邊界
static const uint16_t heapSTRUCT_SIZE =
    ((sizeof(BlockLink_t) + (portBYTE_ALIGNMENT - 1)) & ~portBYTE_ALIGNMENT_MASK);

// 範例：若 sizeof(BlockLink_t) == 9
// (9 + 7) & ~0x7
// = 16 & 0xFFFFFFF8
// = 16
```

### pvPortMalloc() 核心流程

```
暫停排程器（vTaskSuspendAll()）
    ↓
第一次 malloc 就初始化 heap
    ↓
把使用者要的 size 加上 BlockLink_t header（xWantedSize += heapSTRUCT_SIZE）
    ↓
做 alignment
    ↓
從 free list 找一塊夠大的 free block
    ↓
把該 block 從 free list 拿掉
    ↓
如果太大，就切成 allocated block + new free block
    ↓
回傳 header 後面的位址給使用者（user pointer）
    ↓
恢復排程器
```

> **為什麼要 `vTaskSuspendAll()`？** 因為 `malloc` 會修改全域 free list，必須防止 context switch。

**Heap 初始化：**

```c
// 第一次呼叫 pvPortMalloc() 時，heap 還沒被整理成 free list
if (xHeapHasBeenInitialised == pdFALSE)
{
    prvHeapInit();
    xHeapHasBeenInitialised = pdTRUE;
}
```

```
xStart
  │
  ▼
+-----------------------------+
| first big free block        |
| xBlockSize = heap size      |
+-----------------------------+
  │
  ▼
xEnd
```

**使用者呼叫範例：`pvPortMalloc(20)`**

```
20 bytes 使用者資料 + heapSTRUCT_SIZE header + alignment padding
因為之後 vPortFree() 要知道這塊記憶體多大
20 + 8 + 4 (pad) = 32 bytes

allocated block
+--------------------------+
| BlockLink_t / hidden hdr |
+--------------------------+
| pointer returned to user |
| user usable memory       |
+--------------------------+

user_ptr = block_start + heapSTRUCT_SIZE;
```

**關鍵程式碼片段：**

```c
xWantedSize += heapSTRUCT_SIZE;  // 使用者要的大小會被加上 header

if (pxBlock != &xEnd)  // 如果走到 xEnd，代表沒有任何 block 夠大
    pvReturn == NULL;

pvReturn =
    (void *)
    (((uint8_t *) pxPreviousBlock->pxNextFreeBlock)
      + heapSTRUCT_SIZE);
// 跳過 BlockLink_t header，回傳 user memory 的起始位址
// 轉成 uint8_t * 是因為每次加 1 等於加 1 byte
```

---

## 12. vPortFree() 核心流程

> heap_2.c 的 `vPortFree()` 不會把相鄰 free block 合併。

```
使用者傳進來 user pointer
    ↓
往前退 heapSTRUCT_SIZE
    ↓
找到 hidden BlockLink_t header（pxLink->xBlockSize）
    ↓
把這塊 block 插回 free list（prvInsertBlockIntoFreeList((BlockLink_t *)pxLink)）
    ↓
更新剩餘 heap 大小（xFreeBytesRemaining += pxLink->xBlockSize）
```

---

## 13. vPrintFreeList() — Debug 輔助函式

`vPrintFreeList()` 是用來 debug FreeRTOS heap_2.c free list 的輔助函式。它從 `xStart.pxNextFreeBlock` 開始走訪 linked list，直到遇到 `xEnd` 為止。

- 每個 `curNode` 都是一個 free block 起始位址（`BlockLink_t` header 放在開頭）
- 透過 `curNode->xBlockSize` 可以知道該 free block 的總大小
- `curNode + xBlockSize` 就是該 block 的 end address

**用途：**
- 觀察 `pvPortMalloc()` 是否 split block
- 觀察 `vPortFree()` 是否把 block 插回 free list
- 觀察 heap_2.c 不會 coalesce 相鄰 free block 所造成的 fragmentation 現象

---

## 14. prvInsertBlockIntoFreeList — 合併相鄰 Free Block

把剛剛 free 掉的 memory block 插回 free list，並嘗試和左右相鄰的 free block 合併。

```c
// 第一部分：建立要插入的新 Block
BlockLink_t *pxBlockPtr = pxBlockToInsert;

// 初始狀態
// 0x1000          0x1020          0x1040
// +-------------+-------------+
// |  Free A     |  Free B     |
// +-------------+-------------+
//                ^
//                pxBlockToInsert

size_t xStartAddress = (size_t)pxBlockPtr;                  // 取得 0x1020
size_t xEndAddress   = xStartAddress + xBlockSize;           // 取得 0x1040

// 第二部分：掃描整個 Free List
pxPrevBlock = &xStart;
pxCurBlock  = xStart.pxNextFreeBlock;

// 第三部分：取得目前 Block 的範圍
// xCurBlockStartAddr = 0x1000
// xCurBlockEndAddr   = 0x1020

// 第四部分：判斷有沒有相鄰
if (xStartAddress != xCurBlockEndAddr && xEndAddress != xCurBlockStartAddr)
    // 沒有相鄰，不合併

// 第五部分：目前 Block 在前（合併）
if (xStartAddress == xCurBlockEndAddr)  // 0x1020 == 0x1020
// 0x1000      0x1020      0x1040
// +---------+---------+
// | Free A  | Free B  |
// +---------+---------+
//      ↓ 合併後
// 0x1000                0x1040
// +--------------------+
// |      Free 64       |
// +--------------------+
    pxBlockPtr = pxCurBlock;
    pxBlockPtr->xBlockSize += xBlockSize;

// 第六部分：新 Block 在前（合併）
// xEndAddress == xCurBlockStartAddr
// 0x1000      0x1020      0x1040
// +---------+---------+
// | New     | Cur     |
// +---------+---------+
    pxBlockPtr->xBlockSize += xCurBlockSize;

// 第七部分：移除被吃掉的 Block
pxPrevBlock->pxNextFreeBlock = pxCurBlock->pxNextFreeBlock;

// 第八部分：重新插入 Free List（heap_2 依照 Size 排序）
```

---

## 15. Heap 策略比較

| 項目 | heap_2 | heap_4 |
|------|--------|--------|
| Free list 排序 | 依 block size 排序 | 依 address 排序 |
| 合併相鄰 block | ❌ 不合併 | ✅ 自動 coalescing |
| Fragmentation | 容易產生 | 較少 |
| 記憶體利用率 | 較低 | 較高 |


---

## 16. USB_MP3

是一個 STM32F407-based USB audio playback system。硬體上使用STM32F407VGTx，外接 USB flash drive 作為audio source，透過 USB OTG FS host + MSC class 讀取檔案；檔案系統使用 FatFs；audio output path 使用 I2S3 搭配 DMA 傳送16-bit PCM samples 到 CS43L22 codec；control path 則包含 HC-05 Bluetooth module 走USART3 DMA ReceiveToIdle，以及 PA0 EXTI button；UI path 使用 I2C 16x2 LCD 顯示播放狀態與音量。現在的設計採用 super-loop + ISR + FSM，沒有啟用FreeRTOS，USBH_USE_OS=0，因此所有背景處理都依賴 main loop 持續呼叫MX_USB_HOST_Process()。

第一層是 storage layer：USB Host library 維護 MSC device state，FatFs 負責 mount、directory scan、file open/read。

第二層是 audio streaming layer：waveplayer.c 使用 4096-byte audio buffer。以 44.1 kHz、16-bit、stereo 估算，throughput 約 176.4 KB/s，半個 2048-byte buffer 只有約 11.6 ms 的 refill window，所以任何f_read() latency、USB retry、LCD blocking delay 或 high-priority interrupt 都可能造成 underrun。

第三層是 control/UI layer：Bluetooth command frame 用 0xAA + length + command + checksum 的概念控制next、previous、volume、pause、resume，按鍵 EXTI 也會改變播放狀態。
