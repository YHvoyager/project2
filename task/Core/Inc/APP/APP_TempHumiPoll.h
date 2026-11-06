#ifndef __APP_TEMPHUMIPOLL_H__
#define __APP_TEMPHUMIPOLL_H__

#include "FreeRTOS.h"
#include "task.h"

/* ============================================================
 * 温湿度采集任务（应用层）
 * 传感器：RS485 温湿度变送器，Modbus-RTU 从站
 *   0x0000 = 湿度，原始值 ÷ 10 = %RH
 *   0x0001 = 温度，原始值 ÷ 10 = ℃（负温为 16 位补码）
 * ============================================================ */

/* ---------------- 传感器规格（换设备改这里） ---------------- */
#define TH_SLAVE_ADDR       0x01     /* 从站地址 */
#define TH_REG_HUMI         0x0000   /* 湿度寄存器 */
#define TH_REG_TEMP         0x0001   /* 温度寄存器 */
#define TH_VAL_SCALE        10       /* 原始值 ÷ 10 = 物理值 */

/* ---------------- 任务配置 ---------------- */
#define TH_TASK_STACK       384      /* 单位：字（1 字 = 4 字节 = 1.5KB） */
#define TH_TASK_PRIORITY    3        /* 高于 task1/task2，低于 task3 */
#define TH_TASK_NAME        "TempHumi"   /* 最长 9 字符（configMAX_TASK_NAME_LEN=10） */
#define TH_POLL_PERIOD_MS   2000     /* 轮询周期（毫秒） */

/* ---------------- 接口 ---------------- */
extern TaskHandle_t th_task_handle;

/* 静态创建采集任务，在 start_task 中调用一次
 * 返回 pdPASS 成功，pdFAIL 失败 */
BaseType_t TempHumi_TaskCreate(void);

#endif
