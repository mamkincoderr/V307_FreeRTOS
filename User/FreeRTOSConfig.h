/*
 * FreeRTOSConfig.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  FreeRTOS на CH32V307 (ядро V4F). Основа — H417_FreeRTOS (там V5F) и пример WCH EVT V307 FreeRTOS.
 */
#ifndef USER_FREERTOSCONFIG_H_
#define USER_FREERTOSCONFIG_H_

#include <stdint.h>
#include "debug.h"                  // порту нужны NVIC и номера прерываний

void FAULT_Assert(const char *File, uint32_t Line);

// CLINT нет: тик от SysTick, переключение задач — программное прерывание
#define configMTIME_BASE_ADDRESS                ( 0 )
#define configMTIMECMP_BASE_ADDRESS             ( 0 )

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1                   // V307.Ticks для времени задач, LED1
#define configCPU_CLOCK_HZ                      ( SystemCoreClock ) // SysTick тактируется от HCLK (96 МГц)
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 8 )
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 256 )   // слов
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 40 * 1024 ) )    // ОЗУ 128 КБ (у H417 — 96 КБ из 256)
#define configMAX_TASK_NAME_LEN                 ( 12 )
#define configUSE_TRACE_FACILITY                1                   // список задач на веб-странице
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_MUTEXES                       1
#define configQUEUE_REGISTRY_SIZE               0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_COUNTING_SEMAPHORES           1
#define configGENERATE_RUN_TIME_STATS           1                   // загрузка CPU и время задач, мкс
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()        V307_RunTime()
uint64_t V307_RunTime(void);
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configUSE_TASK_NOTIFICATIONS            1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            ( 512 )

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_xSemaphoreGetMutexHolder        1

// Место ошибки — в Fault (RAM без обнуления), LED2 мигает часто, затем сброс (см. main.c)
#define configASSERT( x )                       if ( ( x ) == 0 ) {FAULT_Assert(__FILE__, __LINE__);}

#endif /* USER_FREERTOSCONFIG_H_ */
