/**
  ******************************************************************************
  * @file    FreeRTOS_main.h
  * @brief   FreeRTOS 任务与启动接口的对外声明
  ******************************************************************************
  * 说明：
  *   FreeRTOS_main.c 里把任务函数、任务栈、TCB 都定义成了文件作用域的对象，
  *   外部（main.c）只需要一个启动入口 freertos_start()。
  *   所以这个头文件只暴露这一个函数，其余细节留在 .c 内部，避免重复定义。
  ******************************************************************************
  */

#ifndef __FREERTOS_MAIN_H
#define __FREERTOS_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

/**
  * @brief  创建启动任务并开启调度器
  * @note   内部会调用 xTaskCreateStatic() 和 vTaskStartScheduler()，
  *         调度器正常运行后不会返回；若返回说明堆/栈不足，调度器启动失败。
  * @retval None
  */
void freertos_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __FREERTOS_MAIN_H */
