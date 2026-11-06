#ifndef __W5500_H
#define __W5500_H

#include "main.h"
#include "FreeRTOS.h"   /* BaseType_t */

/* ---------- 片选 ---------- */
#define W5500_CS_PORT       GPIOE
#define W5500_CS_PIN        GPIO_PIN_13

/* ---------- BSB：地址空间分段 ----------
 * ★ 本模块实测的布局是「每个 socket 占 4 个 BSB」：
 *     4n+1 = socket n 寄存器，4n+2 = 发送缓存，4n+3 = 接收缓存
 *   而不是 W5500 数据手册写的 1+n / 0x09+n / 0x11+n。
 *   socket 0 的寄存器块两种算法都得 1，所以寄存器读写一直正常，
 *   只有收发缓存差异暴露出来（读回全 0、发出去是乱码）。
 *   依据：卖家例程用 s*0x20+0x08 / +0x10 / +0x18 这三组值。 */
#define W5500_BSB_COMMON    0x00
#define W5500_BSB_SREG(n)   (0x01 + 4 * (n))
#define W5500_BSB_TXBUF(n)  (0x02 + 4 * (n))
#define W5500_BSB_RXBUF(n)  (0x03 + 4 * (n))

/* ---------- 通用寄存器（BSB=0x00） ---------- */
#define W5500_REG_MR        0x0000
#define W5500_REG_GAR       0x0001
#define W5500_REG_SUBR      0x0005
#define W5500_REG_SHAR      0x0009
#define W5500_REG_SIPR      0x000F
#define W5500_REG_PHYCFGR   0x002E
#define W5500_REG_VERSIONR  0x0039

/* ---------- socket 寄存器（块内偏移） ---------- */
#define Sn_MR        0x0000     /* 协议：1=TCP */
#define Sn_CR        0x0001     /* 命令 */
#define Sn_SR        0x0003     /* 状态 */
#define Sn_PORT      0x0004     /* 本地端口，2 字节大端 */
#define Sn_MSSR      0x0012     /* 最大报文段长度，2 字节 */
#define Sn_TX_FSR    0x0020     /* 发送空闲空间 */
#define Sn_TX_WR     0x0024     /* 发送写指针 */
#define Sn_RX_RSR    0x0026     /* 已收到字节数 */
#define Sn_RX_RD     0x0028     /* 接收读指针 */

/* ---------- Sn_CR 命令码 ----------
   ★ 命令码和状态值必须用不同前缀：曾经 SOCK_LISTEN 被定义两次
     （0x02 命令 / 0x14 状态），后定义的 0x14 胜出，写进 Sn_CR 是非法命令，
     芯片静默丢弃，害得 LISTEN 死活不生效。 */
#define W5500_CMD_OPEN      0x01
#define W5500_CMD_LISTEN    0x02
#define W5500_CMD_CONNECT   0x04
#define W5500_CMD_DISCON    0x08
#define W5500_CMD_CLOSE     0x10
#define W5500_CMD_SEND      0x20
#define W5500_CMD_RECV      0x40

/* ---------- Sn_SR 状态值 ---------- */
#define SOCK_CLOSED        0x00
#define SOCK_INIT          0x13
#define SOCK_LISTEN        0x14
#define SOCK_ESTABLISHED   0x17
#define SOCK_CLOSE_WAIT    0x1C

/* ---------- 接口 ---------- */
void     W5500_Init(void);
void     W5500_SetNetInfo(void);
uint8_t  W5500_ReadReg8(uint16_t addr, uint8_t bsb);
void     W5500_WriteReg8(uint16_t addr, uint8_t bsb, uint8_t val);
void     W5500_ReadBurst(uint16_t addr, uint8_t bsb, uint8_t *buf, uint16_t len);
void     W5500_WriteBurst(uint16_t addr, uint8_t bsb, const uint8_t *buf, uint16_t len);
uint8_t  W5500_TcpOpen(uint8_t sn, uint16_t port);
BaseType_t W5500_Net_TaskCreate(void);

#endif
