/*
 * web.c
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  Соединения обслуживаются по одному, ответ с Connection: close — браузеру этого хватает.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "web.h"
#include "main.h"
#include "ethif.h"
#include "net.h"
#include "log.h"
#include "lwip/stats.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"

st_Web Web;

// Задача веб-сервера
void WEB_Start(void)
{
    xTaskCreate(WEB_Task, "web", WEB_TASK_STACK, NULL, WEB_TASK_PRIO, NULL);
}

//=====================================================================
// Задача веб-сервера
//=====================================================================
// Ждём соединение на 80-м порту, отвечаем, закрываем
void WEB_Task(void *pvParameters)
{
    struct netconn *Conn;

    Web.Listen = netconn_new(NETCONN_TCP);
    netconn_bind(Web.Listen, IP_ADDR_ANY, WEB_PORT);
    netconn_listen(Web.Listen);
    LOG_Add("Web: port %u", WEB_PORT);
    while (1)
    {
        if (netconn_accept(Web.Listen, &Conn) == ERR_OK)
        {
            netconn_set_recvtimeout(Conn, WEB_RX_TIMEOUT_MS);
            WEB_Serve(Conn);
            netconn_close(Conn);
            netconn_delete(Conn);
        }
    }
}

// Один запрос: строка GET, выбор ответа по пути
void WEB_Serve(struct netconn *Conn)
{
    struct netbuf *Nb;
    char *Path, *End;
    uint16_t Len;

    if (netconn_recv(Conn, &Nb) != ERR_OK) {return;}
    Len = netbuf_copy(Nb, Web.Req, WEB_REQ_SIZE - 1);
    netbuf_delete(Nb);
    Web.Req[Len] = 0;
    Web.Requests++;

    if (strncmp(Web.Req, "GET ", 4) != 0) {return;}
    Path = Web.Req + 4;
    End = strchr(Path, ' ');
    if (End != NULL) {*End = 0;}

    Web.Len = 0;
    if ((strcmp(Path, "/") == 0) || (strcmp(Path, "/index.html") == 0))
    {
        WEB_Send(Conn, "text/html; charset=utf-8", WEB_Page, WEB_PageLen, 0);
    }
    else if (strcmp(Path, "/api/status") == 0)
    {
        WEB_StatusJson();
        WEB_Send(Conn, "application/json", Web.Buf, Web.Len, 1);
    }
    else if (strcmp(Path, "/api/meas") == 0)
    {
        WEB_MeasJson();
        WEB_Send(Conn, "application/json", Web.Buf, Web.Len, 1);
    }
    else if (strcmp(Path, "/api/log") == 0)
    {
        Web.Len = LOG_Read(Web.Buf, WEB_BUF_SIZE);
        WEB_Send(Conn, "text/plain; charset=utf-8", Web.Buf, Web.Len, 1);
    }
    else
    {
        Web.NotFound++;
        netconn_write(Conn, WEB_NOT_FOUND, sizeof(WEB_NOT_FOUND) - 1, NETCONN_NOCOPY);
    }
}

// Ответ 200: заголовок и тело (страница — без копирования, она во флеш-образе)
void WEB_Send(struct netconn *Conn, const char *Type, const void *Body, uint32_t Len, uint8_t Copy)
{
    uint16_t HeadLen;

    HeadLen = snprintf(Web.Head, sizeof(Web.Head), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                       "Cache-Control: no-store\r\nConnection: close\r\n\r\n", Type, (unsigned long)Len);
    netconn_write(Conn, Web.Head, HeadLen, NETCONN_COPY);
    netconn_write(Conn, Body, Len, (Copy == 1) ? NETCONN_COPY : NETCONN_NOCOPY);
}

// Дописать в тело ответа
void WEB_Add(const char *Fmt, ...)
{
    va_list Args;
    int n;

    if (Web.Len >= (WEB_BUF_SIZE - 1)) {return;}
    va_start(Args, Fmt);
    n = vsnprintf(Web.Buf + Web.Len, WEB_BUF_SIZE - Web.Len, Fmt, Args);
    va_end(Args);
    if (n > 0) {Web.Len += n;}
    if (Web.Len > (WEB_BUF_SIZE - 1)) {Web.Len = WEB_BUF_SIZE - 1;}
}

// Строка JSON в кавычках: кавычки, обратную косую и служебные символы заменяем на '?'
void WEB_AddText(const char *Text)
{
    char Safe[32];
    uint8_t i;

    for (i = 0; (i < (sizeof(Safe) - 1)) && (Text[i] != 0); i++)
    {
        Safe[i] = ((Text[i] == '"') || (Text[i] == '\\') || ((uint8_t)Text[i] < 0x20)) ? '?' : Text[i];
    }
    Safe[i] = 0;
    WEB_Add("\"%s\"", Safe);
}

// Поле JSON с адресом IPv4 (порядок сети): "Key":"a.b.c.d",
void WEB_AddIp(const char *Key, uint32_t Ip)
{
    WEB_Add("\"%s\":\"%lu.%lu.%lu.%lu\",", Key, (unsigned long)(Ip & 0xFF), (unsigned long)((Ip >> 8) & 0xFF),
            (unsigned long)((Ip >> 16) & 0xFF), (unsigned long)(Ip >> 24));
}

// Состояние одним JSON
void WEB_StatusJson(void)
{
    uint32_t *Uid = (uint32_t *)0x1FFFF7E8;
    uint32_t Up = xTaskGetTickCount() / 1000;
    struct dhcp *Dhcp = netif_dhcp_data(&EthIf.Netif);

    WEB_Add("{\"fw\":\"%s\",\"chip\":\"%08lX\",\"uid\":\"%08lX%08lX%08lX\",\"core\":%lu,\"hclk\":%lu,",
            FW_VERSION, (unsigned long)V307.ChipId, (unsigned long)Uid[0], (unsigned long)Uid[1], (unsigned long)Uid[2],
            (unsigned long)(V307.CoreClock/1000000), (unsigned long)(V307.Hclk/1000000));
    WEB_Add("\"up\":%lu,\"cpu\":%u,\"heap\":%u,\"heapmin\":%u,\"rst\":\"%08lX\",",
            (unsigned long)Up, (unsigned)V307.CpuLoad, (unsigned)xPortGetFreeHeapSize(),
            (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned long)V307.ResetCause);
    WEB_Add("\"fault\":{\"n\":%lu,\"type\":%lu,\"mcause\":\"%08lX\",\"mepc\":\"%08lX\",\"mtval\":\"%08lX\",\"line\":%lu,\"up\":%lu,\"text\":",
            (unsigned long)Fault.Count, (unsigned long)Fault.Type, (unsigned long)Fault.Mcause, (unsigned long)Fault.Mepc,
            (unsigned long)Fault.Mtval, (unsigned long)Fault.Line, (unsigned long)Fault.Uptime);
    WEB_AddText(Fault.Text);
    WEB_Add("},\"eth\":{\"link\":%u,\"speed\":%u,\"fd\":%u,\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"phy\":\"%04X:%04X\",",
            EthIf.FL_Link, (EthIf.FL_Speed100 == 1) ? 100 : 10, EthIf.FL_FullDuplex,
            EthIf.Mac[0], EthIf.Mac[1], EthIf.Mac[2], EthIf.Mac[3], EthIf.Mac[4], EthIf.Mac[5], EthIf.PhyId1, EthIf.PhyId2);
    WEB_Add("\"phyres\":%lu,\"pnflip\":%lu,\"reinit\":%lu,\"irqlink\":%lu,\"pn\":%u,\"tr\":%u,",
            (unsigned long)EthIf.PhyResets, (unsigned long)EthIf.PnFlips, (unsigned long)EthIf.ReInitCnt,
            (unsigned long)EthIf.IrqLinkCnt, (unsigned)EthIf.PN, (unsigned)EthIf.TRDetectStep);
    WEB_Add("\"rx\":%lu,\"tx\":%lu,\"rxerr\":%lu,\"drop\":%lu,\"busy\":%lu,\"nolink\":%lu,\"stall\":%lu,\"buserr\":%lu,\"irq\":%lu,\"ups\":%lu,\"wirx\":%lu,\"wiempty\":%lu,\"wtrx\":%lu,\"irqrx\":%lu,\"irqf\":\"%08lX\"},",
            (unsigned long)EthIf.RxFrames, (unsigned long)EthIf.TxFrames, (unsigned long)EthIf.RxErr, (unsigned long)EthIf.RxDrop,
            (unsigned long)EthIf.TxBusy, (unsigned long)EthIf.TxNoLink, (unsigned long)EthIf.RxStall, (unsigned long)EthIf.BusErr,
            (unsigned long)EthIf.IrqCnt, (unsigned long)EthIf.LinkUpCnt, (unsigned long)EthIf.WakeIrqRx,
            (unsigned long)EthIf.WakeIrqEmpty, (unsigned long)EthIf.WakeTimeoutRx, (unsigned long)EthIf.IrqRxCnt,
            (unsigned long)EthIf.IrqFlags);
    WEB_Add("\"ip\":{\"addr\":\"%s\",\"rx\":%u,\"tx\":%u,\"drop\":%u,\"tcprx\":%u,\"tcptx\":%u,\"tcpdrop\":%u,\"memused\":%u,\"memmax\":%u,\"memerr\":%u},",
            Net.IpText, (unsigned)lwip_stats.ip.recv, (unsigned)lwip_stats.ip.xmit, (unsigned)lwip_stats.ip.drop,
            (unsigned)lwip_stats.tcp.recv, (unsigned)lwip_stats.tcp.xmit, (unsigned)lwip_stats.tcp.drop,
            (unsigned)lwip_stats.mem.used, (unsigned)lwip_stats.mem.max, (unsigned)lwip_stats.mem.err);
    WEB_Add("\"dhcp\":{\"state\":%u,\"bound\":%u,\"binds\":%lu,\"age\":%lu,\"lease\":%lu,",
            (Dhcp != NULL) ? Dhcp->state : 0, Net.FL_Bound, (unsigned long)Net.BoundCnt,
            (unsigned long)((Net.FL_Bound == 1) ? ((xTaskGetTickCount() - Net.BoundTime) / 1000) : 0),
            (unsigned long)((Dhcp != NULL) ? Dhcp->offered_t0_lease : 0));
    WEB_AddIp("mask", Net.Mask);
    WEB_AddIp("gw", Net.Gw);
    WEB_AddIp("server", (Dhcp != NULL) ? ip4_addr_get_u32(ip_2_ip4(&Dhcp->server_ip_addr)) : 0);
    Web.Len--;                                  // запятая после последнего поля
    WEB_Add("},\"web\":%lu,", (unsigned long)Web.Requests);
    WEB_TasksJson();
    WEB_Add("}");
}

// Задачи FreeRTOS: имя, состояние, приоритет, свободный стек (слов), доля CPU с пуска (‰)
void WEB_TasksJson(void)
{
    static const char State[] = "RrBSD?";
    configRUN_TIME_COUNTER_TYPE Total = 0;
    UBaseType_t n, i;
    TaskStatus_t *T;

    n = uxTaskGetSystemState(Web.Tasks, WEB_MAX_TASKS, &Total);
    if (Total == 0) {Total = 1;}
    WEB_Add("\"tasks\":[");
    for (i = 0; i < n; i++)
    {
        T = &Web.Tasks[i];
        WEB_Add("%s{\"n\":", (i == 0) ? "" : ",");
        WEB_AddText(T->pcTaskName);
        WEB_Add(",\"s\":\"%c\",\"p\":%u,\"st\":%u,\"cpu\":%u}",
                State[(T->eCurrentState < 5) ? T->eCurrentState : 5], (unsigned)T->uxCurrentPriority,
                (unsigned)T->usStackHighWaterMark, (unsigned)((T->ulRunTimeCounter * 1000) / Total));
    }
    WEB_Add("]");
}

// Число с фиксированной точкой: Value * Scale с округлением (printf newlib-nano без float)
long WEB_Fix(float Value, float Scale)
{
    float x = Value * Scale;

    return (long)((x >= 0.0f) ? (x + 0.5f) : (x - 0.5f));
}

// Измерения одним JSON. Данные — копии из почтовых ящиков АЦП и системы: задачи adc и led их не ждут.
// Каналы: [U, Umin, Umax] в десятитысячных вольта и T NTC в сотых градуса; температура кристалла — в сотых градуса, загрузка сети — в промилле
void WEB_MeasJson(void)
{
    st_AdcmMeas *M = &Web.Meas;
    st_SysStat *S = &Web.Sys;
    uint8_t i;

    xQueuePeek(Adcm.MeasQ, M, 0);
    xQueuePeek(V307.SysQ, S, 0);
    taskENTER_CRITICAL();
    memcpy(Web.IpText, Net.IpText, sizeof(Web.IpText));
    taskEXIT_CRITICAL();
    Web.IpText[sizeof(Web.IpText) - 1] = 0;

    WEB_Add("{\"w\":%lu,\"t\":%lu,\"rate\":%lu,\"tau\":%ld,\"ch\":[", (unsigned long)M->Window,
            (unsigned long)M->Tick, (unsigned long)M->SweepRate, WEB_Fix(M->FiltTau, 10.0f));
    for (i = 0; i < ADCM_CHANNELS; i++)
    {
        WEB_Add("%s[%ld,%ld,%ld,%ld]", (i == 0) ? "" : ",", WEB_Fix(M->U[i], 10000.0f), WEB_Fix(M->Umin[i], 10000.0f),
                WEB_Fix(M->Umax[i], 10000.0f), WEB_Fix(M->T[i], 100.0f));
    }
    WEB_Add("],\"isr\":%u,\"ref\":{\"vdda\":%ld,\"vref\":%ld,\"code\":%u,\"inj\":%lu},", M->IsrLoadPm, WEB_Fix(M->Vdda, 10000.0f),
            WEB_Fix(M->Vrefint, 10000.0f), M->CodeVref, (unsigned long)M->InjRate);
    WEB_Add("\"temp\":{\"t\":%ld,\"u\":%ld,\"code\":%u,\"calu\":%u,\"calt\":%u,\"p\":[%ld,%ld,%ld]},",
            WEB_Fix(M->Tcpu, 100.0f), WEB_Fix(M->Uts, 10.0f), M->CodeTs, M->CalMv, M->CalT,
            WEB_Fix(ADCM_TS_P1, 1000000.0f), WEB_Fix(ADCM_TS_P2, 1000000.0f), WEB_Fix(ADCM_TS_P3, 1000000.0f));
    WEB_Add("\"sys\":{\"up\":%lu,\"cpu\":%u,\"heap\":%lu,\"heapmin\":%lu,\"rx\":%lu,\"tx\":%lu,\"rxf\":%lu,"
            "\"txf\":%lu,\"load\":%u,\"link\":%u,\"fd\":%u,\"ip\":\"%s\",\"core\":%lu}}",
            (unsigned long)(S->Uptime / 1000), S->CpuLoad, (unsigned long)S->HeapFree, (unsigned long)S->HeapMin,
            (unsigned long)S->RxBps, (unsigned long)S->TxBps, (unsigned long)S->RxFps, (unsigned long)S->TxFps,
            S->LoadPm, S->Link, S->FullDuplex, Web.IpText, (unsigned long)(V307.CoreClock / 1000000));
}
