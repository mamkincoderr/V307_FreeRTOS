/*
 * log.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  Журнал событий в RAM: последние строки видны на веб-странице (/api/log) и в отладчике (Log.Line).
 *  На V307 новые строки ещё и уходят в UART встроенного WCH-Link (задача led). Строки — по-английски.
 */
#ifndef USER_LOG_H_
#define USER_LOG_H_

#include <stdint.h>

#define LOG_LINES       32
#define LOG_LINE_LEN    96
#define LOG_UART        USART1          // PA9, скорость — UART_BAUD (main.h)

typedef struct
{
    char Line[LOG_LINES][LOG_LINE_LEN];
    uint32_t Count;                     // записей с пуска; новая идёт в Line[Count % LOG_LINES]
    uint32_t UartNext;                  // следующая запись для UART
} st_Log;

extern st_Log Log;

void LOG_Add(const char *Fmt, ...)          __attribute__((optimize("-Os"), format(printf, 1, 2)));
uint32_t LOG_Read(char *Buf, uint32_t Size) __attribute__((optimize("-Os")));
void LOG_UART_DO(void)                      __attribute__((optimize("-Os")));
void LOG_UartPut(char Ch)                   __attribute__((optimize("-Os")));

#endif /* USER_LOG_H_ */
