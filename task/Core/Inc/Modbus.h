#ifndef __MODBUS_H__
#define __MODBUS_H__

#include "stm32f4xx_hal.h"

/* ============================================================
 * Modbus-RTU 主站协议层（组帧 / CRC / 解析）
 * 不关心具体设备：从站地址、寄存器含义由调用方传入
 * 底层收发走 RS485.h
 * ============================================================ */

/* ---------------- 功能码 ---------------- */
#define MB_FC_READ_HOLDING   0x03U   /* 读保持寄存器 */
#define MB_FC_WRITE_SINGLE   0x06U   /* 写单个寄存器 */

/* ---------------- 异常应答的功能码 ----------------
 * 从站出错时把请求的功能码最高位置 1 返回
 *   0x03 | 0x80 = 0x83
 *   0x06 | 0x80 = 0x86
 */
#define MB_EX_READ_HOLDING   0x83U
#define MB_EX_WRITE_SINGLE   0x86U

/* ---------------- 返回码 ---------------- */
#define MB_OK        0   /* 成功 */
#define MB_TIMEOUT   1   /* 发送失败 / 等应答超时 */
#define MB_CRC_ERR   2   /* CRC 校验不通过 */
#define MB_RSP_ERR   3   /* 应答字段不符 / 从站返回异常码 */
#define MB_PARAM_ERR 4   /* 调用方传参非法 */

/* ---------------- 可调参数 ---------------- */
#define MB_RSP_TIMEOUT_MS   200U   /* 等应答超时（毫秒） */
#define MB_MAX_READ_REGS    8U     /* 一次最多读几个寄存器 */
#define MB_CRC_LEN          2U     /* CRC 占 2 个字节，配合 sizeof 用 */

/* ---------------- 接口 ---------------- */

/* 读保持寄存器
 * 成功返回 MB_OK，out[0..num-1] 为寄存器原始值（已从大端转主机序） */
int Modbus_ReadHoldingRegs(uint8_t addr, uint16_t startReg,
                       uint16_t num, uint16_t *out);

/* 写单个寄存器（应答是请求的原样回显，回显一致才算成功） */
int Modbus_WriteSingleReg(uint8_t addr, uint16_t reg, uint16_t val);

#endif
