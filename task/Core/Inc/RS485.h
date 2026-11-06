#ifndef __RS485_H__
#define __RS485_H__

#include "stm32f4xx_hal.h"

/* 返回码 */
#define RS485_OK        0 // 成功
#define RS485_ERR_TX   -1 // 发送错误
#define RS485_ERR_TMO  -2 // 超时
#define RS485_ERR_PE   -3 // 奇偶校验错误
#define RS485_ERR_ORE  -4 // 过出错误
#define RS485_ERR_ARG  -5 // 参数错误

void RS485_ClearRX(void);
void RS485_Init(void);
int RS485_Send(const uint8_t *buf, uint16_t len);
int RS485_Recv(uint8_t *buf, uint16_t buf_size, uint16_t *out_len, uint32_t timeout_ms);

#endif