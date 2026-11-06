#include "RS485.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "string.h"
#include "stdio.h"

#define RS485_UART huart2
#define RS485_DE_PORT MAX485_EN_GPIO_Port
#define RS485_DE_PIN MAX485_EN_Pin

#define RS485_TX_TIMEOUT_MS 100
#define RS485_TC_TIMEOUT_MS 10

/* 发送完成同步 */
static SemaphoreHandle_t tx_done_sem;
static SemaphoreHandle_t rx_done_sem;
static uint8_t  rs485_rx_buf[64];
static volatile uint16_t rs485_rx_len;

//
// @brief 设置 RS485 方向
// @param tx 1: 发送, 0: 接收
//
static void RS485_SetDir(uint8_t tx)
{
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, tx ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void RS485_ClearRX(void)
{
    uint32_t t0 = HAL_GetTick();

    while (__HAL_UART_GET_FLAG(&RS485_UART, UART_FLAG_RXNE) != RESET && (HAL_GetTick() - t0) < 2U)
    {
        (void)RS485_UART.Instance->DR;
    }

    __HAL_UART_CLEAR_OREFLAG(&RS485_UART);
    __HAL_UART_CLEAR_FEFLAG(&RS485_UART);
    __HAL_UART_CLEAR_NEFLAG(&RS485_UART);
    __HAL_UART_CLEAR_PEFLAG(&RS485_UART);
}

void RS485_Init(void)
{
    RS485_SetDir(0);                                 /* 1. 先做接收方 */
    RS485_ClearRX();                                 /* 2. 倒掉上电时线上的垃圾 */
    RS485_UART.ErrorCode = HAL_UART_ERROR_NONE;      /* 3. 清错误码 */

    tx_done_sem = xSemaphoreCreateBinary();
    rx_done_sem = xSemaphoreCreateBinary();
    configASSERT(tx_done_sem != NULL);
    configASSERT(rx_done_sem != NULL);
}

int RS485_Send(const uint8_t *buf, uint16_t len)
{
    // #1.判断发送缓存区是否为空
    if (buf == NULL || len == 0)
        return RS485_ERR_ARG;

    // #2.发送前清理+抢总线
    /* 清掉上次超时后可能残留的信号 */
    (void)xSemaphoreTake(tx_done_sem, 0);

    RS485_ClearRX();
    RS485_SetDir(1);

    // #3.启动DMA：可以立刻返回，后台在搬数据
    if (HAL_UART_Transmit_DMA(&RS485_UART, (uint8_t *)buf, len) != HAL_OK)
    {
        RS485_SetDir(0);
        return RS485_ERR_TX;  // 发送错误
    }

    // #4.阻塞等待完成，期间让出CPU
    if (xSemaphoreTake(tx_done_sem, pdMS_TO_TICKS(RS485_TX_TIMEOUT_MS)) != pdTRUE)
    {
        (void)HAL_UART_AbortTransmit(&RS485_UART);   /* 超时就掐断 DMA */
        RS485_SetDir(0);
        return RS485_ERR_TX;  // 发送错误
    }

    return RS485_OK;  // 发送成功
}

int RS485_Recv(uint8_t *buf, uint16_t buf_size, uint16_t *out_len, uint32_t timeout_ms)
{
    // #1.判断接收区是否合法
    if (buf == NULL || buf_size == 0 || out_len == NULL)
        return RS485_ERR_ARG;  // 参数错误

    // #2.清残留
    (void)xSemaphoreTake(rx_done_sem, 0);

    // #3.IDLE + DMA :收多少是多少，总线空闲时触发回调
    if (HAL_UARTEx_ReceiveToIdle_DMA(&RS485_UART, rs485_rx_buf,
                                     sizeof(rs485_rx_buf)) != HAL_OK)
        return RS485_ERR_TMO;  // 接收超时

    if (xSemaphoreTake(rx_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        (void)HAL_UART_AbortReceive(&RS485_UART);   /* 超时掐断 */
        return RS485_ERR_TMO;  // 接收超时
    }

    *out_len = rs485_rx_len;
    if (*out_len > buf_size) return RS485_ERR_ARG;   /* 调用方缓冲不够 */
    memcpy(buf, rs485_rx_buf, *out_len);

    return RS485_OK;  // 接收成功
}

//
// @简介：发送完成回调函数
//
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;

    if (huart->Instance == USART2)
    {
        RS485_SetDir(0);                        /* 先交还总线 */
        xSemaphoreGiveFromISR(tx_done_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}


//
// @简介：接收事件回调函数
//
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    BaseType_t woken = pdFALSE;

    if (huart->Instance == USART2)
    {
        rs485_rx_len = Size;
        xSemaphoreGiveFromISR(rx_done_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

