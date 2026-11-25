/*
 * net.c
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/apps/mdns.h"
#include "lwip/apps/netbiosns.h"
#include "net.h"
#include "ethif.h"
#include "web.h"
#include "log.h"

st_Net Net;

// Случайное число для lwIP (номера портов, ISN TCP, mDNS): xorshift32 от UID и времени
uint32_t NET_Rand(void)
{
    uint32_t x = Net.RandState;

    if (x == 0) {x = (*(uint32_t *)0x1FFFF7E8) ^ (*(uint32_t *)0x1FFFF7EC) ^ xTaskGetTickCount() ^ 0x9E3779B9;}
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    Net.RandState = x;
    return x;
}

//=====================================================================
// Задача сети
//=====================================================================
// Поднимаем lwIP и службы, дальше — приём кадров и опрос PHY. Адрес — от DHCP роутера:
// до ответа интерфейс без адреса, после переподключения кабеля lwIP запрашивает адрес заново
void NET_Task(void *pvParameters)
{
    EthIf.Task = xTaskGetCurrentTaskHandle();
    tcpip_init(NULL, NULL);
    strcpy(Net.IpText, "0.0.0.0");

    LOCK_TCPIP_CORE();
    netif_add(&EthIf.Netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, ETHIF_NetifInit, tcpip_input);
    netif_set_default(&EthIf.Netif);
    netif_set_status_callback(&EthIf.Netif, NET_StatusCallback);
    netif_set_up(&EthIf.Netif);
    dhcp_start(&EthIf.Netif);       // клиент DHCP
    mdns_resp_init();               // v307.local
    mdns_resp_add_netif(&EthIf.Netif, NET_HOSTNAME);
    mdns_resp_add_service(&EthIf.Netif, NET_MDNS_SERVICE, "_http", DNSSD_PROTO_TCP, 80, NULL, NULL);
    netbiosns_init();               // http://v307 в Windows
    netbiosns_set_name(NET_HOSTNAME);
    UNLOCK_TCPIP_CORE();

    ETHIF_IrqEnable();
    WEB_Start();
    Net.FL_Up = 1;
    LOG_Add("Net up: DHCP client, MAC %02X:%02X:%02X:%02X:%02X:%02X, PHY ID %04X:%04X",
            EthIf.Mac[0], EthIf.Mac[1], EthIf.Mac[2], EthIf.Mac[3], EthIf.Mac[4], EthIf.Mac[5],
            EthIf.PhyId1, EthIf.PhyId2);

    while (1)
    {
        ETHIF_DO();
    }
}

// Адрес интерфейса изменился (задача tcpip): получен по DHCP или снят — в Net и в журнал
void NET_StatusCallback(struct netif *netif)
{
    Net.Ip = ip4_addr_get_u32(netif_ip4_addr(netif));
    Net.Mask = ip4_addr_get_u32(netif_ip4_netmask(netif));
    Net.Gw = ip4_addr_get_u32(netif_ip4_gw(netif));
    ip4addr_ntoa_r(netif_ip4_addr(netif), Net.IpText, NET_IP_TEXT_LEN);
    if (Net.Ip != 0)
    {
        Net.FL_Bound = 1;
        Net.BoundCnt++;
        Net.BoundTime = xTaskGetTickCount();
        LOG_Add("DHCP: %s, mask %lu.%lu.%lu.%lu, gw %lu.%lu.%lu.%lu", Net.IpText,
                (unsigned long)(Net.Mask & 0xFF), (unsigned long)((Net.Mask >> 8) & 0xFF),
                (unsigned long)((Net.Mask >> 16) & 0xFF), (unsigned long)(Net.Mask >> 24),
                (unsigned long)(Net.Gw & 0xFF), (unsigned long)((Net.Gw >> 8) & 0xFF),
                (unsigned long)((Net.Gw >> 16) & 0xFF), (unsigned long)(Net.Gw >> 24));
    }
    else if (Net.FL_Bound == 1)
    {
        Net.FL_Bound = 0;
        LOG_Add("DHCP: address released");
    }
}

// Загрузка сети за прошедшее время (раз в секунду из задачи led): байт/с, кадров/с и доля линии 10 Мбит/с.
// Кадр занимает линию данными + NET_FRAME_OVERHEAD; в полном дуплексе считаем худшее направление, в полу — сумму
void NET_LOAD_CALC(void)
{
    uint32_t Now = xTaskGetTickCount();
    uint32_t dt = Now - Net.LoadTime;
    uint32_t dRx, dTx, dRxF, dTxF;
    uint64_t Bits;

    if (dt == 0) {return;}
    dRx = EthIf.RxBytes - Net.RxBytesLast;
    dTx = EthIf.TxBytes - Net.TxBytesLast;
    dRxF = EthIf.RxFrames - Net.RxFramesLast;
    dTxF = EthIf.TxFrames - Net.TxFramesLast;
    Net.RxBytesLast = EthIf.RxBytes;
    Net.TxBytesLast = EthIf.TxBytes;
    Net.RxFramesLast = EthIf.RxFrames;
    Net.TxFramesLast = EthIf.TxFrames;
    Net.LoadTime = Now;
    Net.RxBps = (uint64_t)dRx * 1000 / dt;
    Net.TxBps = (uint64_t)dTx * 1000 / dt;
    Net.RxFps = (uint64_t)dRxF * 1000 / dt;
    Net.TxFps = (uint64_t)dTxF * 1000 / dt;
    if (EthIf.FL_FullDuplex == 1)
    {
        Bits = 8ULL * ((dRx + dRxF * NET_FRAME_OVERHEAD) > (dTx + dTxF * NET_FRAME_OVERHEAD) ?
                       (dRx + dRxF * NET_FRAME_OVERHEAD) : (dTx + dTxF * NET_FRAME_OVERHEAD));
    }
    else
    {
        Bits = 8ULL * (dRx + dTx + (dRxF + dTxF) * NET_FRAME_OVERHEAD);
    }
    Net.LoadPm = (uint16_t)(Bits * 1000000ULL / ((uint64_t)NET_LINK_BPS * dt));
}
