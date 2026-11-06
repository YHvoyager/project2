/* ============================================================
 * 温湿度采集任务（应用层）
 *
 * 【当前是占位桩】
 * 只为让工程能编译通过 —— FreeRTOS_main.c 里调用了
 * TempHumi_TaskCreate()，没有定义会链接失败。
 *
 * 等 RS485 / Modbus 两层写完、调通，再回来实现任务体。
 * ============================================================ */
#include "APP/APP_TempHumiPoll.h"
#include "Modbus.h"
#include "stdio.h"
#include "stdlib.h"

/* 启动温湿度传感器任务的配置 */
#define TempHumiPoll_STACK 384
#define TempHumiPoll_PRIORITY 3
TaskHandle_t TempHumiPoll_handle;
StackType_t TempHumiPoll_stack[TempHumiPoll_STACK]; // 静态任务的任务栈以数组存储
StaticTask_t TempHumiPoll_tcb;                      // 静态任务的TCB结构体类型
void TempHumiPoll_Task(void *pvParameters);

TaskHandle_t th_task_handle = NULL;

BaseType_t TempHumiPoll_TaskCreate(void)
{
    TempHumiPoll_handle = xTaskCreateStatic(
        (TaskFunction_t)TempHumiPoll_Task,
        (char *)"TempHumiPoll",
        (configSTACK_DEPTH_TYPE)TempHumiPoll_STACK,
        (void *)NULL,
        (UBaseType_t)TempHumiPoll_PRIORITY, // 任务优先级
        (StackType_t *)TempHumiPoll_stack,  // 任务栈的地址  我们创建的任务栈(数组存储)的地址
        (StaticTask_t *)&TempHumiPoll_tcb); // 我们创建的tcb的地址

    return (th_task_handle != NULL) ? pdPASS : pdFAIL;
}

static void TempHumiPoll_Task(void *pvParameters)
{
    TickType_t xLastWakeTime;
    uint16_t regs[2];
    int16_t t_raw;
    int ret;

    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();
    printf("[TH] task start, %d ms\r\n", TH_POLL_PERIOD_MS);

    for (;;)
    {
        /* 从湿度寄存器起连读 2 个（湿度 + 温度相邻） */
        ret = Modbus_ReadHoldingRegs(TH_SLAVE_ADDR, TH_REG_HUMI, 2, regs);

        if (ret == MB_OK)
        {
            t_raw = (int16_t)regs[1]; /* 负温是 16 位补码 */

            printf("[TH] Humi: %d.%d %%RH   Temp: %d.%d C\r\n",
                   regs[0] / 10, regs[0] % 10,   /* 559 → 55.9 */
                   t_raw / 10, abs(t_raw % 10)); /* -101 → -10.1 */
        }
        else
        {
            printf("[TH] read fail, ret=%d\r\n", ret);
        }

        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TH_POLL_PERIOD_MS));
    }
}
