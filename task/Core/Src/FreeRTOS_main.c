// freeRTOS相关头文件
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"
#include "APP/APP_TempHumiPoll.h"
#include "Modbus.h"
#include "W5500.H"

/* 启动任务的配置 */
#define START_TASK_STACK 256
#define START_TASK_PRIORITY 1
TaskHandle_t start_task_handle;
StackType_t start_task_stack[START_TASK_STACK]; // 静态任务的任务栈以数组存储
StaticTask_t start_task_tcb;                    // 静态任务的TCB结构体类型
static void start_task(void *pvParameters);

#define TASK1_STACK 256
#define TASK1_PRIORITY 1
TaskHandle_t task1_handle;
StackType_t task1_stack[TASK1_STACK]; // 静态任务的任务栈以数组存储
StaticTask_t task1_tcb;                    // 静态任务的TCB结构体类型
static void task1(void *pvParameters);

// 静态创建方式,需要手动指定两个特殊任务的资源
/* 空闲任务的配置 */
StackType_t idle_task_stack[configMINIMAL_STACK_SIZE]; // 静态任务的任务栈以数组存储
StaticTask_t idle_task_tcb;                            // 静态任务的TCB结构体类型

/* 软件定时器任务配置 */
StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH]; // 静态任务的任务栈以数组存储
StaticTask_t timer_task_tcb;                                // 静态任务的TCB结构体类型

//@ 简介：分配空闲任务的内存资源
//
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
//
//@ 简介：分配软件定时器任务的内存资源
//
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
//
// @ 简介：启动FreeRTOS
//
void freertos_start(void)
{
    // # 1.创建启动任务
    start_task_handle = xTaskCreateStatic((TaskFunction_t)start_task,       // 任务函数地址
                                          (char *)"start_task",             // 任务名称字符串
                                          (uint32_t)START_TASK_STACK,       // 任务堆栈大小，默认最小128，单位4字节
                                          (void *)NULL,                     // 传递给任务函数的参数
                                          (UBaseType_t)START_TASK_PRIORITY, // 任务优先级
                                          (StackType_t *)start_task_stack,  // 任务栈的地址  我们创建的任务栈(数组存储)的地址
                                          (StaticTask_t *)&start_task_tcb); // 我们创建的tcb的地址

    // # 2.启动调度器：会自动创建空闲任务和软件定时器(如果开启)，
    // 静态创建的方式需要去实现两个分配资源的接口函数
    vTaskStartScheduler();
}

//
// @ 简介：启动任务用来创建其他task
// @ 参数：
uint8_t TempumiPoll_status = 0;
uint8_t W5500_Task_status = 0;
void start_task(void *pvParameters)
{
    /* 进入临界区代码:保护临界区里的代码不会被打断 */
    taskENTER_CRITICAL();
    /* 创建任务 */

    TempumiPoll_status = TempHumiPoll_TaskCreate();
    if (TempumiPoll_status == pdPASS)
    {
        printf("[TH] task create success\r\n");
    }
    else
    {
        printf("[TH] task create fail\r\n");
    }

    W5500_Task_status = W5500_TaskCreate();
    if (W5500_Task_status == pdPASS)
    {
        printf("[W5500_Task] task create success\r\n");
    }
    else
    {
        printf("[W5500_Task] task create fail\r\n");
    }
    
       task1_handle = xTaskCreateStatic(
        (TaskFunction_t)task1,
        (char *)"task1",
        (configSTACK_DEPTH_TYPE)TASK1_STACK,
        (void *)NULL,
        (UBaseType_t)TASK1_PRIORITY, // 任务优先级
        (StackType_t *)task1_stack,  // 任务栈的地址  我们创建的任务栈(数组存储)的地址
        (StaticTask_t *)&task1_tcb); // 我们创建的tcb的地址

    /* 删除启动任务自己（启动任务只需要执行一次即可） */
    vTaskDelete(NULL);
    /* 退出临界区代码 */
    taskEXIT_CRITICAL();
}

//
// @简介：任务1：LED1 500ms闪烁一次
//
void task1(void *pvParameters)
{

    while (1)
    {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6);
        vTaskDelay(2000);
    }
}