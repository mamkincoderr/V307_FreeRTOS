/*
 * web.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  Веб-сервер на netconn: страница (web/index.html, вшита при сборке) и JSON состояния.
 *    /             страница
 *    /api/meas     измерения: 12 каналов, Vrefint/VDDA, температура, CPU и сеть (JSON, 4–5 раз в секунду)
 *    /api/status   состояние платы, сети, задач (JSON)
 *    /api/log      журнал (текст)
 */
#ifndef USER_WEB_H_
#define USER_WEB_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/api.h"
#include "adcm.h"
#include "main.h"

#define WEB_PORT            80
#define WEB_TASK_STACK      1024            // слов
#define WEB_TASK_PRIO       3
#define WEB_RX_TIMEOUT_MS   3000
#define WEB_REQ_SIZE        512
#define WEB_BUF_SIZE        6144
#define WEB_MAX_TASKS       16
#define WEB_NOT_FOUND       "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n" \
                            "Connection: close\r\n\r\nNot found"

typedef struct
{
    struct netconn *Listen;
    char Req[WEB_REQ_SIZE];                 // начало запроса
    char Buf[WEB_BUF_SIZE];                 // тело ответа
    char Head[192];                         // заголовок ответа
    uint32_t Len;                           // занято в Buf
    TaskStatus_t Tasks[WEB_MAX_TASKS];
    st_AdcmMeas Meas;                       // копии из почтовых ящиков (xQueuePeek)
    st_SysStat Sys;
    char IpText[16];
    uint32_t Requests;
    uint32_t NotFound;
} st_Web;

extern st_Web Web;
extern const uint8_t WEB_Page[];
extern const uint32_t WEB_PageLen;

void WEB_Start(void)                                    __attribute__((optimize("-Os")));
void WEB_Task(void *pvParameters)                       __attribute__((optimize("-Os")));
void WEB_Serve(struct netconn *Conn)                    __attribute__((optimize("-Os")));
void WEB_Send(struct netconn *Conn, const char *Type,
              const void *Body, uint32_t Len, uint8_t Copy)  __attribute__((optimize("-Os")));
void WEB_Add(const char *Fmt, ...)                      __attribute__((optimize("-Os"), format(printf, 1, 2)));
void WEB_AddText(const char *Text)                      __attribute__((optimize("-Os")));
void WEB_AddIp(const char *Key, uint32_t Ip)            __attribute__((optimize("-Os")));
void WEB_StatusJson(void)                               __attribute__((optimize("-Os")));
void WEB_TasksJson(void)                                __attribute__((optimize("-Os")));
void WEB_MeasJson(void)                                 __attribute__((optimize("-Os")));
long WEB_Fix(float Value, float Scale)                  __attribute__((optimize("-Os")));

#endif /* USER_WEB_H_ */
