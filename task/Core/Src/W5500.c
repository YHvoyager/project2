/* W5500 驱动（精简版）
 *
 * 三个必须遵守的点，都是踩出来的：
 *   1. 一次事务 = 一次 HAL_SPI_TransmitReceive，帧头和数据在同一个 CS 低电平窗口里连着发完。
 *      分成两次 HAL 调用的话，中间 HAL 会关掉 SPE，SCK 上可能多出边沿，芯片就错位。
 *   2. 控制字节的 OM 用固定长度（0x01 = 1 字节）。变长模式靠数时钟判长度，对毛刺零容忍。
 *   3. 命令码和状态值用不同宏前缀（W5500_CMD_* / SOCK_*）。曾经同名覆盖，
 *      LISTEN 实际下发非法命令 0x14，芯片静默丢弃，查了两小时。
 */
#include "W5500.h"
#include "spi.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

extern SPI_HandleTypeDef hspi2;

#define W5500_OM_VAR   0x00     /* 变长：连续读写用 */
#define W5500_OM_1B    0x01     /* 固定 1 字节：单寄存器用 */

#define W5500_IO_MAX   256      /* 单次收发上限 */
static uint8_t s_tx[3 + W5500_IO_MAX];   /* SPI 帧缓冲（帧头 + 数据） */
static uint8_t s_rx[3 + W5500_IO_MAX];   /* SPI 接收缓冲 */
static uint8_t io_buf[W5500_IO_MAX];     /* 用户数据用途，和 SPI 缓冲分开，免得互相踩 */

/* ---------------- 片选 ---------------- */
static void W5500_ENABLE(void)  { HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_RESET); }
static void W5500_DISABLE(void) { HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_SET);   }

/* 把 3 字节帧头写进 s_tx 前三位 */
static void frame_start(uint16_t addr, uint8_t bsb, uint8_t rwb, uint8_t om)
{
    s_tx[0] = (uint8_t)(addr >> 8);
    s_tx[1] = (uint8_t)(addr & 0xFF);
    s_tx[2] = (uint8_t)((bsb << 3) | (rwb << 2) | om);   /* BSB | RWB | OM */
}

/* ---------------- 寄存器读写 ---------------- */

uint8_t W5500_ReadReg8(uint16_t addr, uint8_t bsb)
{
    frame_start(addr, bsb, 0, W5500_OM_1B);
    s_tx[3] = 0xFF;

    W5500_ENABLE();
    HAL_SPI_TransmitReceive(&hspi2, s_tx, s_rx, 4, 10);
    W5500_DISABLE();

    return s_rx[3];
}

void W5500_WriteReg8(uint16_t addr, uint8_t bsb, uint8_t val)
{
    frame_start(addr, bsb, 1, W5500_OM_1B);
    s_tx[3] = val;

    W5500_ENABLE();
    HAL_SPI_TransmitReceive(&hspi2, s_tx, s_rx, 4, 10);
    W5500_DISABLE();
}

void W5500_ReadBurst(uint16_t addr, uint8_t bsb, uint8_t *buf, uint16_t len)
{
    if (len == 0 || len > W5500_IO_MAX) return;

    frame_start(addr, bsb, 0, W5500_OM_VAR);
    memset(&s_tx[3], 0xFF, len);

    W5500_ENABLE();
    HAL_SPI_TransmitReceive(&hspi2, s_tx, s_rx, (uint16_t)(3 + len), 100);
    W5500_DISABLE();

    memcpy(buf, &s_rx[3], len);
}

void W5500_WriteBurst(uint16_t addr, uint8_t bsb, const uint8_t *buf, uint16_t len)
{
    if (len == 0 || len > W5500_IO_MAX) return;

    frame_start(addr, bsb, 1, W5500_OM_VAR);
    memcpy(&s_tx[3], buf, len);

    W5500_ENABLE();
    HAL_SPI_TransmitReceive(&hspi2, s_tx, s_rx, (uint16_t)(3 + len), 100);
    W5500_DISABLE();
}

/* 16 位寄存器（大端） */
static uint16_t read16(uint16_t addr, uint8_t bsb)
{
    uint8_t hi = W5500_ReadReg8(addr, bsb);
    uint8_t lo = W5500_ReadReg8((uint16_t)(addr + 1), bsb);
    return (uint16_t)((hi << 8) | lo);
}

static void write16(uint16_t addr, uint8_t bsb, uint16_t val)
{
    W5500_WriteReg8(addr, bsb, (uint8_t)(val >> 8));
    W5500_WriteReg8((uint16_t)(addr + 1), bsb, (uint8_t)(val & 0xFF));
}

/* ---------------- 命令 ---------------- */

/* 等 socket 状态变成期望值，超时返回 1 */
static uint8_t wait_sr(uint8_t sn, uint8_t want, uint16_t ms)
{
    uint8_t bsb = W5500_BSB_SREG(sn);
    while (ms--)
    {
        if (W5500_ReadReg8(Sn_SR, bsb) == want) return 0;
        HAL_Delay(1);
    }
    return 1;
}

/* 发命令 + 等目标状态。
   ★ 不能只等 Sn_CR 清零：命令码非法时芯片同样会把 CR 清零，看着"成功"其实啥也没干。
     必须校验 Sn_SR 真的迁到了目标状态。 */
static uint8_t sock_cmd(uint8_t sn, uint8_t cmd, uint8_t want_sr, uint16_t ms)
{
    W5500_WriteReg8(Sn_CR, W5500_BSB_SREG(sn), cmd);
    return wait_sr(sn, want_sr, ms);
}

/* 发不改变 Sn_SR 的命令（SEND / RECV），只需等 CR 被清掉 */
static void cmd_only(uint8_t sn, uint8_t cmd)
{
    uint8_t  bsb = W5500_BSB_SREG(sn);
    uint32_t t0;

    W5500_WriteReg8(Sn_CR, bsb, cmd);
    t0 = HAL_GetTick();
    while (W5500_ReadReg8(Sn_CR, bsb) != 0)
    {
        if ((HAL_GetTick() - t0) > 50) break;   /* 超时也放行，别把任务挂死 */
    }
}

/* ---------------- 初始化 ---------------- */

void W5500_Init(void)
{
    /* 1) SPI2 分频：42MHz / 16 = 2.6MHz（先求稳，通了可改小分频） */
    HAL_SPI_Init(&hspi2);

    /* 2) CS 空闲高 */
    W5500_DISABLE();
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 3) 软件复位：W5500 的 socket 状态不随 MCU 复位清除，必须让它自己复位一次。
          复位会清掉 IP/MAC，所以 SetNetInfo 必须放在这之后。 */
    W5500_WriteReg8(W5500_REG_MR, W5500_BSB_COMMON, 0x80);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void W5500_SetNetInfo(void)
{
    static const uint8_t mac[6] = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33};
    static const uint8_t ip[4]  = {192, 168, 1, 200};
    static const uint8_t sn[4]  = {255, 255, 255, 0};
    static const uint8_t gw[4]  = {192, 168, 1, 1};

    W5500_WriteBurst(W5500_REG_SHAR, W5500_BSB_COMMON, mac, 6);
    W5500_WriteBurst(W5500_REG_SIPR, W5500_BSB_COMMON, ip,  4);
    W5500_WriteBurst(W5500_REG_SUBR, W5500_BSB_COMMON, sn,  4);
    W5500_WriteBurst(W5500_REG_GAR,  W5500_BSB_COMMON, gw,  4);
}

/* ---------------- TCP 服务端 ---------------- */

/* 返回 0 = 已在监听 */
uint8_t W5500_TcpOpen(uint8_t sn, uint16_t port)
{
    uint8_t bsb = W5500_BSB_SREG(sn);

    sock_cmd(sn, W5500_CMD_CLOSE, SOCK_CLOSED, 100);   /* 先确保关干净 */

    write16(Sn_PORT, bsb, port);
    write16(Sn_MSSR, bsb, 1460);                       /* TCP 必需，别省 */
    W5500_WriteReg8(Sn_MR, bsb, 0x01);                 /* TCP */

    if (sock_cmd(sn, W5500_CMD_OPEN,   SOCK_INIT,   100)) return 1;
    if (sock_cmd(sn, W5500_CMD_LISTEN, SOCK_LISTEN, 100)) return 2;
    return 0;
}

/* ---------------- 任务 ---------------- */

/* 启动W5500任务的配置 */
#define W5500_TASK_STACK 512
#define W5500_TASK_PRIORITY 3
TaskHandle_t W5500_Task_handle;
StackType_t W5500_Task_task_stack[W5500_TASK_STACK]; // 静态任务的任务栈以数组存储
StaticTask_t W5500_Task_tcb;                      // 静态任务的TCB结构体类型
void W5500_Task(void *pvParameters);

static void W5500_Task(void *pv);

BaseType_t W5500_TaskCreate(void)
{
    W5500_Task_handle = xTaskCreateStatic(
        (TaskFunction_t)W5500_Task,
        (char *)"W5500_Task",
        (configSTACK_DEPTH_TYPE)W5500_TASK_STACK,
        (void *)NULL,
        (UBaseType_t)W5500_TASK_PRIORITY, // 任务优先级
        (StackType_t *)W5500_Task_task_stack,  // 任务栈的地址  我们创建的任务栈(数组存储)的地址
        (StaticTask_t *)&W5500_Task_tcb); // 我们创建的tcb的地址

    return (W5500_Task_handle != NULL) ? pdPASS : pdFAIL;   
}

/* 把收到的数据原样回发，验证 RX/TX 通路 */
static void echo_once(uint8_t sn)
{
    uint8_t  bsb = W5500_BSB_SREG(sn);
    uint16_t len = read16(Sn_RX_RSR, bsb);
    if (len == 0) return;
    if (len > W5500_IO_MAX) len = W5500_IO_MAX;

    uint16_t rd = read16(Sn_RX_RD, bsb);
    W5500_ReadBurst(rd, W5500_BSB_RXBUF(sn), io_buf, len);

    write16(Sn_RX_RD, bsb, (uint16_t)(rd + len));
    cmd_only(sn, W5500_CMD_RECV);                          /* 告诉芯片这段读完了 */

    if (read16(Sn_TX_FSR, bsb) < len) return;              /* 发送缓存不够就丢弃 */

    uint16_t wr = read16(Sn_TX_WR, bsb);
    W5500_WriteBurst(wr, W5500_BSB_TXBUF(sn), io_buf, len);
    write16(Sn_TX_WR, bsb, (uint16_t)(wr + len));
    cmd_only(sn, W5500_CMD_SEND);

    printf("[NET] echo %u bytes\r\n", len);
}

static void W5500_Task(void *pv)
{
    uint8_t sn  = 0;
    uint8_t bsb = W5500_BSB_SREG(sn);

    W5500_Init();
    W5500_SetNetInfo();
    W5500_TcpOpen(sn, 5000);

    for (;;)
    {
        switch (W5500_ReadReg8(Sn_SR, bsb))
        {
        case SOCK_ESTABLISHED:
            echo_once(sn);
            break;

        case SOCK_CLOSE_WAIT:                     /* 对端断开，本端跟着关 */
            sock_cmd(sn, W5500_CMD_DISCON, SOCK_CLOSED, 100);
            break;

        case SOCK_CLOSED:                         /* 掉线，重新监听 */
            W5500_TcpOpen(sn, 5000);
            break;

        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
