/*
 * log.c
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "debug.h"
#include "log.h"

st_Log Log;

// Строка в журнал с меткой времени от пуска: "  12.345 текст"
void LOG_Add(const char *Fmt, ...)
{
    char Text[LOG_LINE_LEN];
    uint32_t Ms = xTaskGetTickCount();
    uint32_t Pos;
    va_list Args;

    Pos = snprintf(Text, sizeof(Text), "%5lu.%03lu ", (unsigned long)(Ms/1000), (unsigned long)(Ms%1000));
    va_start(Args, Fmt);
    vsnprintf(Text + Pos, sizeof(Text) - Pos, Fmt, Args);
    va_end(Args);

    taskENTER_CRITICAL();
    memcpy(Log.Line[Log.Count % LOG_LINES], Text, LOG_LINE_LEN);
    Log.Count++;
    taskEXIT_CRITICAL();
}

// Журнал текстом, от старых строк к новым; возвращает длину
uint32_t LOG_Read(char *Buf, uint32_t Size)
{
    uint32_t First, i, Len = 0, n;

    First = (Log.Count > LOG_LINES) ? (Log.Count - LOG_LINES) : 0;
    for (i = First; i < Log.Count; i++)
    {
        n = strnlen(Log.Line[i % LOG_LINES], LOG_LINE_LEN);
        if ((Len + n + 2) > Size) {break;}
        memcpy(Buf + Len, Log.Line[i % LOG_LINES], n);
        Len += n;
        Buf[Len++] = '\n';
    }
    return Len;
}

// Новые строки журнала — в UART WCH-Link; отстали больше чем на LOG_LINES — только последние
void LOG_UART_DO(void)
{
    uint32_t Count = Log.Count;
    char *Line;
    uint32_t i;

    if ((Count - Log.UartNext) > LOG_LINES) {Log.UartNext = Count - LOG_LINES;}
    while (Log.UartNext != Count)
    {
        Line = Log.Line[Log.UartNext % LOG_LINES];
        for (i = 0; (i < LOG_LINE_LEN) && (Line[i] != 0); i++)
        {
            LOG_UartPut(Line[i]);
        }
        LOG_UartPut(0x0D);
        LOG_UartPut(0x0A);
        Log.UartNext++;
    }
}

// Символ в UART: ждём свободный буфер передачи
void LOG_UartPut(char Ch)
{
    while (USART_GetFlagStatus(LOG_UART, USART_FLAG_TXE) == RESET) {}
    USART_SendData(LOG_UART, (uint16_t)Ch);
}
