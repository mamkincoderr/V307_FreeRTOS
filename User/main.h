/*
 * main.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  CH32V307 (ядро V4F, 112 МГц): FreeRTOS + lwIP, вольтметр 12 каналов + термодатчик и Vrefint, вывод по Ethernet.
 *  Основа — H417_FreeRTOS (24.11.2026); 25.11 светодиоды, кнопка и проверка FPU убраны.
 */
#ifndef USER_MAIN_H_
#define USER_MAIN_H_

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define FW_VERSION          "1.0.0"

#define SYS_TASK_PERIOD_MS  250         // задача sys: сторожевой, журнал в UART
#define SYS_STAT_PERIOD_MS  1000        // загрузка CPU и сети, почтовый ящик системы
#define SYS_TASK_STACK      512         // слов
#define SYS_TASK_PRIO       2
#define UART_BAUD           115200      // журнал в UART WCH-Link (USART1, PA9)
#define IWDG_PRESCALER      IWDG_Prescaler_64
#define IWDG_RELOAD         0x0FFF      // LSI ~40 кГц / 64 * 4096 = ~6,5 с
#define FAULT_MAGIC         0x46415535  // "FAU5": запись о сбое годная (смена значения стирает старые)

// Причина сбоя
typedef enum
{
    FAULT_NONE = 0,
    FAULT_EXCEPTION,
    FAULT_ASSERT,
    FAULT_STACK,
    FAULT_MALLOC
} en_Fault;

// Последний сбой: в RAM без обнуления, переживает сброс
typedef struct
{
    uint32_t Magic;
    uint32_t Count;                 // сбоев с подачи питания
    uint32_t Type;                  // en_Fault
    uint32_t Mcause;
    uint32_t Mepc;
    uint32_t Mtval;
    uint32_t Line;
    uint32_t Uptime;                // мс от пуска до сбоя
    char     Text[32];              // файл assert или имя задачи
} st_Fault;

// Снимок состояния раз в секунду, в RAM без обнуления: подключение WCH-Link сбрасывает чип,
// и отладчик видит только первые ~50 мс — а снимок показывает, что было до сброса
typedef struct
{
    uint32_t Magic;
    uint32_t Boot;                  // пусков с подачи питания
    uint32_t Uptime;                // мс
    uint8_t  Link;
    uint8_t  Speed100;
    uint8_t  FullDuplex;
    uint8_t  CpuLoad;
    uint16_t Bsr;                   // регистры PHY: 1, 0, 5, 0x1A
    uint16_t Bcr;
    uint16_t Anlpar;
    uint16_t PhyStat;
    uint32_t LinkUpCnt;
    uint32_t RxFrames;
    uint32_t TxFrames;
    uint32_t RxErr;
    uint32_t IrqCnt;
    uint32_t BusErr;
    uint32_t Ip;                    // адрес от DHCP, порядок сети
    uint32_t DhcpBinds;
    uint32_t WebReq;
    uint32_t AdcWindows;
    uint32_t LastTick;              // последний тик прогона (из ловушки тика)
    char     LastTask[12];          // задача, прерванная последним тиком
    uint32_t ResetCause;            // RSTSCKR этого пуска
} st_Diag;

// Состояние системы раз в секунду — почтовый ящик V307.SysQ (задача sys пишет, веб читает копию)
typedef struct
{
    uint32_t Uptime;                // мс
    uint32_t HeapFree;
    uint32_t HeapMin;
    uint32_t RxBps;                 // байт/с
    uint32_t TxBps;
    uint32_t RxFps;                 // кадров/с
    uint32_t TxFps;
    uint16_t LoadPm;                // загрузка линии 10 Мбит/с, промилле
    uint8_t  CpuLoad;               // %
    uint8_t  Link;
    uint8_t  FullDuplex;
} st_SysStat;

typedef struct
{
    QueueHandle_t SysQ;             // почтовый ящик st_SysStat
    st_SysStat Sys;                 // рабочая копия задачи sys
    uint32_t ChipId;
    uint32_t CoreClock;             // V4F, Гц
    uint32_t Hclk;                  // Гц (= ядро, HPRE ÷1)
    uint32_t CntPerUs;              // тактов SysTick в микросекунде
    volatile uint32_t Ticks;        // тики FreeRTOS (для счётчика времени задач)
    uint8_t  CpuLoad;               // % за последнюю секунду
    uint64_t IdleTimeLast;
    uint64_t RunTimeLast;
    uint32_t CpuCalcTime;           // последний расчёт загрузки CPU, мс
    uint32_t ResetCause;            // RCC->RSTSCKR при пуске
    st_Diag  DiagPrev;              // снимок прошлого прогона, взят при пуске
} st_V307;

extern st_V307 V307;
extern st_Fault Fault;
extern st_Diag Diag;

int main(void)                                  __attribute__((optimize("-Os")));
void IWDG_Setup(void)                           __attribute__((optimize("-Os")));
void SYS_Task(void *pvParameters)               __attribute__((optimize("-Os")));
void CPU_LOAD_CALC(void)                        __attribute__((optimize("-Os")));
void SYS_PUBLISH(void)                          __attribute__((optimize("-Os")));
void DIAG_Snapshot(void)                        __attribute__((optimize("-Os")));
uint64_t V307_RunTime(void)                     __attribute__((optimize("-Ofast")));
void FAULT_Assert(const char *File, uint32_t Line);
void FAULT_Stop(void)                           __attribute__((optimize("-O0"), noreturn));

#endif /* USER_MAIN_H_ */
