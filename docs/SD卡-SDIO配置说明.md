# SD 卡（SDIO 4-bit）配置说明

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-11-06 | 初版。接线核对、时钟前提（PLLQ 必须改 7）、最小验证阶梯、上拉与 FatFS 取舍 |

> 配套 `docs/W5500-以太网驱动技术文档.md`（以太网）与 `docs/RS485-Modbus软件设计文档.md`（串口）。
> SD 卡是独立外设，与这两条链路无引脚交集。

---

## 1. 接线（从原理图 U5 读出，已核对）

| 座子脚 | 信号 | STM32 | SDIO 复用 |
|---|---|---|---|
| 1 | DAT2 | **PC10** | SDIO_D2 |
| 2 | DAT3 | **PC11** | SDIO_D3 |
| 3 | CMD | **PD2** | SDIO_CMD |
| 4 | VDD | 3.3V + C19 100nF 去耦 | — |
| 5 | CLK | **PC12** | SDIO_CK |
| 6 | VSS | GND | — |
| 7 | DAT0 | **PC8** | SDIO_D0 |
| 8 | DAT1 | **PC9** | SDIO_D1 |
| 9 | CD | 只经 R21(1M) 下拉到 GND，**没接 MCU** | — |

**结论**：
- 这是**标准 SDIO 4-bit 接法**，一根线都不用改，直接按 4-bit 模式配。
- **卡检测（CD）没引到 MCU**，程序里不要等 CD 脚，按"卡常驻"处理。
- 工程当前占用：PA2/PA3（USART2）、PA6/PA7（LED）、PC4/PC5（W5500 RST/INT）、PC6/PC7、PE7。
  **PC8~PC12 与 PD2 全空，无冲突** ✅

---

## 2. ★ 先修时钟：PLLQ 现在是错的

SDIO 的时钟源是 **PLL48CLK（PLLQ 输出）**，规范要求 **48MHz**。而你现在的配置：

```c
/* Core/Src/main.c  SystemClock_Config() */
RCC_OscInitStruct.PLL.PLLM = 8;
RCC_OscInitStruct.PLL.PLLN = 168;
RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
RCC_OscInitStruct.PLL.PLLQ = 4;      /* ← 问题在这 */
```

算一下：HSI 16MHz ÷ 8 × 168 = **VCO 336MHz**
- SYSCLK = 336 / 2 = 168MHz ✅
- **PLL48CLK = 336 / 4 = 84MHz ❌（要求 48MHz）**

### 改法一（最小改动）：Q 改成 7

```c
RCC_OscInitStruct.PLL.PLLQ = 7;      /* 336 / 7 = 48MHz ✅ */
```

`PLLP` 和 `PLLQ` 是两条独立支路，**改 Q 不影响 168MHz 主频**。顺带说一句：USB 也用 PLL48CLK，
所以这个值以前一直没被发现——因为没用 USB。

### 改法二（推荐，更稳）：时钟源换成板载 8MHz 晶振

板上有 Y2 = 8MHz HSE，目前没用。SD 卡对主机时钟精度有要求（识别阶段 400kHz、之后高速），
HSI 是内部 RC，全温区 ±1% 漂起来，低速能跑，4-bit 高速下容易偶发 CRC 错。

```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
RCC_OscInitStruct.PLL.PLLM       = 8;    /* 8MHz / 8 = 1MHz */
RCC_OscInitStruct.PLL.PLLN       = 336;  /* 1MHz × 336 = 336MHz VCO */
RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;   /* 168MHz */
RCC_OscInitStruct.PLL.PLLQ       = 7;    /* 48MHz */
```

### 换 HSE 的完整清单（四处，缺一处都会"看起来正常其实全错"）

| # | 位置 | 值 | 漏改的后果 |
|---|---|---|---|
| 1 | CubeMX Clock Configuration「Input frequency」 | **8 MHz** | CubeMX 侧 PLL 参数按 25MHz 算，全错（且要保存 .ioc 才生效） |
| 2 | `Core/Inc/stm32f4xx_hal_conf.h` 的 `HSE_VALUE` | **8000000U** | HAL 按 25MHz 折算主频 → **串口波特率差 3.125 倍，printf 全乱码**；`HAL_Delay` 慢 3 倍 |
| 3 | `main.c` 里 `OscillatorType=HSE` / `PLLSource=PLLSOURCE_HSE` | 见下 | 仍走 HSI，实际 168MHz 但 PLL48CLK 是 84MHz |
| 4 | `PLLM/N/P/Q` = **8 / 336 / 2 / 7** | — | 8÷8=1MHz → ×336=336MHz VCO → ÷2=168MHz，÷7=48MHz |

> 第 2 条是最阴的：**改完能跑，但串口全乱**。因为 `SystemCoreClock` 算错，从来没报过错。
> 本工程已把 `HSE_VALUE` 改成 8000000（原来是 CubeMX 默认的 25000000）。

> ⚠ 换 HSE 前先确认 Y2 真的焊了晶振（原理图上有，实物核对一下）。晶振不起振会直接卡在
> `HAL_RCC_OscConfig()` 里进 `Error_Handler()`——**现象是烧录后串口一个字都不打印**。

---

## 3. SDIO 时钟是怎么算的

```
SDIO_CK = SDIOCLK / (ClockDiv + 2)      ← F4 的 SDIO 是这个公式，不是 /(2×ClockDiv)
```

> 这张表按 `stm32f4xx_ll_sdmmc.h:724` 的注释核过：`SDIO_INIT_CLK_DIV = 0x76` →
> 48MHz/(118+2) = 400kHz；`SDIO_TRANSFER_CLK_DIV = 0x0` → 48MHz/2 = 24MHz。

| 阶段 | ClockDiv | SDIO_CK |
|---|---|---|
| 卡识别（HAL 内部固定 `SDIO_INIT_CLK_DIV`=118） | 118 | 400 kHz ✅ |
| **正常工作（推荐）** | **0** | **24 MHz** |
| 降速备选 | 1 | 16 MHz |
| 降速备选 | 2 | 12 MHz |
| 降速备选 | 6 | 6 MHz |

4-bit @ 24MHz 理论带宽 12MB/s，实测写 2~5MB/s，记录传感器数据绰绰有余。

---

## 4. 工程要补什么（和 SPI 一个套路）

| 动作 | 状态 |
|---|---|
| `stm32f4xx_hal_conf.h:63` 打开 `HAL_SD_MODULE_ENABLED` | ✅ 已改 |
| 拷入 `stm32f4xx_hal_sd.c/.h` + `stm32f4xx_ll_sdmmc.c/.h` | ✅ 已拷（HAL 1.8.5，与工程同版本） |

**Keil 里手动加两个 .c**：`Drivers/STM32F4xx_HAL_Driver` 分组 → Add Existing Files →
`stm32f4xx_hal_sd.c` 和 `stm32f4xx_ll_sdmmc.c`。

> 两个都要加。只加 `hal_sd.c` 会在链接时报一堆 `SDMMC_*` undefined——那些函数在 `ll_sdmmc.c` 里。

### 4.1 CubeMX 里 Mode 选哪个

Connectivity → SDIO → **Mode** 下拉有 6 项，选 **`SD 4 bits Wide bus`**，其余全不对：

| 选项 | 判断 |
|---|---|
| Disable | 不启用 SDIO |
| SD 1 bit | ❌ 只用 DAT0，速度只剩 1/4，DAT1~3 白接 |
| **SD 4 bits Wide bus** | ✅ **选这个**。图上 DAT0~DAT3 四根全接了 |
| MMC 1 bit / 4 bits / 8 bits | ❌ MMC 是 eMMC/MMC 卡，协议不同（没有 CMD8/ACMD41，初始化流程是另一套），插 SD 卡认不出 |

选完之后 CubeMX 会自动把 **PC8/PC9/PC10/PC11/PC12 + PD2** 配成 `AF12_SDIO`，
并在 `hsd.Init.BusWide` 里写 `SDIO_BUS_WIDE_4B`。

#### SDIO parameters 五个选项（全部保持截图里的值）

| 参数 | 取值 | 含义 / 为什么 |
|---|---|---|
| Clock transition on which the bit capture is made | **Rising transition** | SD 规范规定数据在时钟**上升沿**采样。改成 Falling 会全盘读错 |
| SDIO Clock divider bypass | **Disable** | 旁路 = SDIO_CK 直接等于 48MHz，太快要炸。必须走分频 |
| SDIO Clock output enable when the bus is idle | **Disable the power save for the clock** | 总线空闲时时钟**继续输出**。稳定优先；将来做低功耗再改成 Enable |
| SDIO hardware flow control | **The hardware control flow is disabled** | 这是 SDIO 内部的 FIFO 流控（不是串口 RTS/CTS），HAL 例程一律关掉 |
| **SDIOCLK clock divide factor** | **0** | ⚠ 注意：0 表示**除以 2**，即 48MHz/(0+2) = **24MHz**，不是 48MHz。这是最容易误解的一格 |

> 24MHz 落在 SD「默认速度」的 25MHz 上限之内，合法且够用（4-bit 理论 12MB/s）。
> 想上 48MHz 得开 bypass + High Speed 模式，板子上没上拉电阻，别试。

> ⚠ 前提仍然是第 2 节那条：**PLLQ 必须是 7**。如果还是 4，CubeMX 的 Clock Configuration 里
> SDIO 时钟会显示 84MHz（黄色/红色告警），生成的 `ClockDiv` 也会算错。

> ⚠ 再次提醒：**确认完就手工改代码，不要点 Generate Code**——你工程里手工加的
> `FreeRTOS/*`、`Src/APP`、`Inc/APP` 分组会被清掉。

---

## 5. 最小验证（先别碰文件系统）

### 5.1 初始化

```c
#include "stm32f4xx_hal.h"

static SD_HandleTypeDef hsd;

void SD_PortInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;          /* 板上没外部上拉，靠内部约 40k */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO;

    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_2;                 /* CMD */
    HAL_GPIO_Init(GPIOD, &gpio);

    hsd.Instance                  = SDIO;
    hsd.Init.ClockEdge            = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass          = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave       = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide              = SDIO_BUS_WIDE_4B;
    hsd.Init.HardwareFlowControl  = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv             = 0;     /* 48MHz/(0+2) = 24MHz */

    if (HAL_SD_Init(&hsd) != HAL_OK)
    {
        printf("[SD] init fail, err=%lu\r\n", hsd.ErrorCode);
        return;
    }

    /* 再显式切一次 4-bit（HAL_SD_Init 已按 BusWide 配，这里保险） */
    if (HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) != HAL_OK)
    {
        printf("[SD] 4-bit fail, err=%lu\r\n", hsd.ErrorCode);
    }
}
```

### 5.2 判据：读卡信息

```c
HAL_SD_CardInfoTypeDef info;
HAL_SD_CardCIDTypeDef  cid;

HAL_SD_GetCardInfo(&hsd, &info);
HAL_SD_GetCardCID(&hsd, &cid);

printf("[SD] state=%d  %lu blocks x %lu B = %lu MB\r\n",
       HAL_SD_GetCardState(&hsd), info.BlockNbr, info.BlockSize,
       (info.BlockNbr >> 11));           /* 512B/块 → ÷2048 = MB */
printf("[SD] CID name=%.5s  SN=%lu\r\n", (char *)cid.ProdName, cid.ProdSN);
```

**通过判据**：`HAL_SD_GetCardState()` == `HAL_SD_CARD_TRANSFER`(4)，且 `BlockNbr > 0`。
不通过时看 `hsd.ErrorCode`：

| ErrorCode 位 | 典型原因 |
|---|---|
| `HAL_SD_ERROR_CMD_RSP_TIMEOUT` | 卡没插 / CMD(PD2) 断 / 3.3V 没供上 / 上拉太弱 |
| `HAL_SD_ERROR_CMD_CRC_FAIL` | 时钟太快、走线太长、上拉不足 |
| `HAL_SD_ERROR_DATA_TIMEOUT` | DAT 线接触不良 |
| `HAL_SD_ERROR_UNSUPPORTED_FEATURE` | 卡是 SDXC（>32GB），F4 的 SDIO 是 SD 2.0，可能认不出 |
| `HAL_SD_ERROR_ADDR_OUT_OF_RANGE` | 扇区号越界（SDHC 必须用块地址，不是字节地址） |

### 5.3 第二步：读 0 号块验证数据通路

```c
uint8_t buf[512] __attribute__((aligned(4)));

if (HAL_SD_ReadBlocks(&hsd, buf, 0, 1, 1000) == HAL_OK)
{
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER);   /* 等传输完成 */
    printf("[SD] blk0 tail = %02X %02X (有分区表的卡应为 55 AA)\r\n", buf[510], buf[511]);
}
```

> 缓冲区必须 4 字节对齐（HAL 内部走 DMA 通道，未对齐会报 `HAL_SD_ERROR_ADDR_MISALIGNED`）。

---

## 6. 上拉电阻：板上没有，先低速跑

SD 规范要求 CMD 与 DAT0~DAT3 各接 **10~47kΩ 上拉到 3.3V**。U5 这一块只有 C19 去耦，**没有上拉电阻**。

| 现状 | 后果 |
|---|---|
| 靠 MCU 内部上拉（约 40k） | 400kHz 识别、12MHz 以内通常能跑 |
| 24MHz 4-bit 高速 | 可能偶发 CRC 错、写不进、卡掉线 |

**处理顺序**：先按 24MHz（`ClockDiv=0`）跑，出错就改 `ClockDiv=2`（12MHz）复测；
12MHz 仍不稳，就在 CMD 和 4 根 DAT 上飞 10k 上拉到 3.3V（一共 5 个电阻）。

---

## 7. FatFS 要不要上

| | 方案 A：裸 HAL_SD（推荐先用） | 方案 B：CubeMX + FatFS |
|---|---|---|
| 能力 | 按 512B 扇区读写，没有文件名 | `f_open/f_write/f_close`，卡插电脑能直接看 |
| 文件量 | 2 个 .c | FatFS 十几个文件 + `sd_diskio.c` + `ffconf.h` |
| 加进工程 | Keil 加 2 个文件 | 要手工加十几个（**不能 Generate Code**，否则你手工的 FreeRTOS/APP 分组会被清） |
| 适合 | 固定格式的数据记录（自己定块头） | 要 PC 兼容、要日志文件可读 |

**建议**：先用裸驱动把"写第 N 块 → 读回来对比"跑通，确认卡和时序没问题；
真需要 PC 直接读卡上的文件时，再补 FatFS。

---

## 8. 资源占用核对

| 资源 | 用途 | 冲突 |
|---|---|---|
| SDIO | SD 卡 4-bit | 无 |
| PC8~PC12、PD2 | SDIO 信号 | 工程当前全空 ✅ |
| DMA2 Stream3 / Stream6 | SDIO RX / TX（用 DMA 时才占） | `stm32f4xx_it.c` 里现有 DMA2 Stream2/7（USART1），**Stream3/6 空** ✅ |

> 本说明的验证代码全部用**轮询 + 内部 DMA**（`HAL_SD_ReadBlocks` 内部就用 DMA2_Stream3），
> 不需要你另外配 DMA 中断。

---

## 9. 待确认

| # | 事项 | 影响 |
|---|---|---|
| 1 | Y2（8MHz HSE）实物是否焊了 | 决定能不能走"改法二"换 HSE |
| 2 | 手上 SD 卡容量与类型（SDHC ≤32GB？） | SDXC（>32GB）F4 可能不认 |
| 3 | SD 卡的用途（数据记录 / 配置存储 / 导出） | 决定要不要上 FatFS |
