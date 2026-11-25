/*
 * main.c
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  CH32V307: вольтметр 12 каналов + термодатчик и Vrefint. Плата берёт адрес у роутера по DHCP и показывает
 *  веб-страницу (http://v307.local, http://v307, адрес — в журнале UART).
 *  Задачи: net (кадры, PHY), tcpip (lwIP), web (HTTP), adc (окна измерений), sys (сторожевой, CPU, сеть).
 */
#include <string.h>
#include "main.h"
#include "ethif.h"
#include "net.h"
#include "web.h"
#include "log.h"
#include "adcm.h"
#include "cfg.h"

st_V307 V307;
st_Fault Fault __attribute__((section(".noinit")));
st_Diag Diag __attribute__((section(".noinit")));

//=====================================================================
// main
//=====================================================================
int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(UART_BAUD);   // журнал в UART WCH-Link
    IWDG_Setup();                   // сторожевой ~6,5 с, кормит задача sys
    V307.ChipId = DBGMCU_GetCHIPID();
    V307.CoreClock = SystemCoreClock;
    V307.Hclk = SystemCoreClock;
    V307.CntPerUs = SystemCoreClock / 1000000;
    V307.ResetCause = RCC->RSTSCKR;
    RCC->RSTSCKR |= RCC_RMVF;       // флаги причины сброса — заново к следующему пуску
    if (Fault.Magic != FAULT_MAGIC) {memset(&Fault, 0, sizeof(Fault));}  // после подачи питания
    if (Diag.Magic != FAULT_MAGIC)
    {
        memset(&Diag, 0, sizeof(Diag));
        Diag.Magic = FAULT_MAGIC;
    }
    V307.DiagPrev = Diag;
    Diag.Boot++;
    Diag.ResetCause = V307.ResetCause;
//-----
    ETHIF_HW_Init();                // MAC и PHY: Delay_Us ещё можно, SysTick пока свободен
//-----
    CFG_Load();                     // настройки FlowerGraph из флеш
    ADCM_Init();                    // АЦП: 12 каналов по кругу + термодатчик и Vrefint
//-----
    LOG_Add("V307 %s: core %lu MHz, chip %08lX, reset %08lX, faults %lu", FW_VERSION,
            (unsigned long)(V307.CoreClock/1000000), (unsigned long)V307.ChipId,
            (unsigned long)V307.ResetCause, (unsigned long)Fault.Count);

    V307.SysQ = xQueueCreate(1, sizeof(st_SysStat));
    xTaskCreate(NET_Task, "net", NET_TASK_STACK, NULL, NET_TASK_PRIO, NULL);
    xTaskCreate(ADCM_Task, "adc", ADCM_TASK_STACK, NULL, ADCM_TASK_PRIO, NULL);
    xTaskCreate(SYS_Task, "sys", SYS_TASK_STACK, NULL, SYS_TASK_PRIO, NULL);
    vTaskStartScheduler();

    FAULT_Assert(__FILE__, __LINE__);   // сюда приходим, только если не хватило кучи
}

// Сторожевой таймер (в option bytes V307 программный): ÷64, ~6,5 с; кормит задача sys.
// Под отладчиком при остановленном ядре стоит и он
void IWDG_Setup(void)
{
    DBGMCU_Config(DBGMCU_IWDG_STOP, ENABLE);
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_PRESCALER);
    IWDG_SetReload(IWDG_RELOAD);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

//=====================================================================
// Задача sys
//=====================================================================
// Сторожевой и журнал в UART — раз в SYS_TASK_PERIOD_MS; загрузка CPU и сети, почтовый ящик системы
// и снимок Diag — раз в секунду
void SYS_Task(void *pvParameters)
{
    TickType_t Wake = xTaskGetTickCount();

    V307.CpuCalcTime = Wake;
    while (1)
    {
        vTaskDelayUntil(&Wake, pdMS_TO_TICKS(SYS_TASK_PERIOD_MS));
        IWDG_ReloadCounter();       // сторожевой ~6,5 с: задачи живы
        LOG_UART_DO();
        if ((xTaskGetTickCount() - V307.CpuCalcTime) >= pdMS_TO_TICKS(SYS_STAT_PERIOD_MS))
        {
            V307.CpuCalcTime = xTaskGetTickCount();
            CPU_LOAD_CALC();
            NET_LOAD_CALC();
            SYS_PUBLISH();
            DIAG_Snapshot();
        }
    }
}

// Загрузка CPU за секунду: доля времени вне задачи Idle
void CPU_LOAD_CALC(void)
{
    uint64_t Idle = ulTaskGetIdleRunTimeCounter();
    uint64_t Now = V307_RunTime();
    uint64_t dIdle = Idle - V307.IdleTimeLast;
    uint64_t dAll = Now - V307.RunTimeLast;

    V307.IdleTimeLast = Idle;
    V307.RunTimeLast = Now;
    if ((dAll == 0) || (dIdle > dAll)) {return;}
    V307.CpuLoad = 100 - (uint8_t)((dIdle * 100) / dAll);
}

// Состояние системы за секунду — в почтовый ящик для веба
void SYS_PUBLISH(void)
{
    st_SysStat *S = &V307.Sys;

    S->Uptime = xTaskGetTickCount();
    S->HeapFree = xPortGetFreeHeapSize();
    S->HeapMin = xPortGetMinimumEverFreeHeapSize();
    S->RxBps = Net.RxBps;
    S->TxBps = Net.TxBps;
    S->RxFps = Net.RxFps;
    S->TxFps = Net.TxFps;
    S->LoadPm = Net.LoadPm;
    S->CpuLoad = V307.CpuLoad;
    S->Link = EthIf.FL_Link;
    S->FullDuplex = EthIf.FL_FullDuplex;
    xQueueOverwrite(V307.SysQ, S);
}

// Снимок состояния в Diag (RAM без обнуления) — для чтения после сброса
void DIAG_Snapshot(void)
{
    Diag.Uptime = xTaskGetTickCount();
    Diag.Link = EthIf.FL_Link;
    Diag.Speed100 = EthIf.FL_Speed100;
    Diag.FullDuplex = EthIf.FL_FullDuplex;
    Diag.CpuLoad = V307.CpuLoad;
    Diag.Bsr = EthIf.Bsr;
    Diag.Bcr = EthIf.Bcr;
    Diag.Anlpar = EthIf.Anlpar;
    Diag.PhyStat = EthIf.PhyStat;
    Diag.LinkUpCnt = EthIf.LinkUpCnt;
    Diag.RxFrames = EthIf.RxFrames;
    Diag.TxFrames = EthIf.TxFrames;
    Diag.RxErr = EthIf.RxErr;
    Diag.IrqCnt = EthIf.IrqCnt;
    Diag.BusErr = EthIf.BusErr;
    Diag.Ip = Net.Ip;
    Diag.DhcpBinds = Net.BoundCnt;
    Diag.WebReq = Web.Requests;
    Diag.AdcWindows = Web.Meas.Window;
}

// Время для статистики задач, мкс: тики FreeRTOS + счётчик SysTick (HCLK, период 1 мс)
uint64_t V307_RunTime(void)
{
    return (uint64_t)V307.Ticks * 1000 + (uint32_t)SysTick->CNT / V307.CntPerUs;
}

// Тик FreeRTOS: счётчик для времени задач
void vApplicationTickHook(void)
{
    V307.Ticks++;
    Diag.LastTick = V307.Ticks;
    memcpy(Diag.LastTask, pcTaskGetName(NULL), sizeof(Diag.LastTask));
}

// Переполнение стека задачи: запоминаем имя и сбрасываемся
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    Fault.Type = FAULT_STACK;
    strncpy(Fault.Text, pcTaskName, sizeof(Fault.Text) - 1);
    FAULT_Stop();
}

// Не хватило кучи FreeRTOS
void vApplicationMallocFailedHook(void)
{
    Fault.Type = FAULT_MALLOC;
    FAULT_Stop();
}

// configASSERT и LWIP_ASSERT: файл и строка
void FAULT_Assert(const char *File, uint32_t Line)
{
    const char *Name = strrchr(File, '/');

    if (Name == NULL) {Name = strrchr(File, '\\');}
    Name = (Name == NULL) ? File : (Name + 1);
    Fault.Type = FAULT_ASSERT;
    Fault.Line = Line;
    strncpy(Fault.Text, Name, sizeof(Fault.Text) - 1);
    FAULT_Stop();
}

// Сбой: запись в RAM без обнуления, сброс чипа
void FAULT_Stop(void)
{
    portDISABLE_INTERRUPTS();
    Fault.Magic = FAULT_MAGIC;
    Fault.Count++;
    Fault.Uptime = V307.Ticks;
    NVIC_SystemReset();
    while (1) {}
}
