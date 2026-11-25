/*
 * net.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  Подъём сети: lwIP, интерфейс Ethernet, клиент DHCP (адрес от роутера), имена v307.local (mDNS)
 *  и V307 (NetBIOS), веб-сервер. Задача сети после подъёма сама принимает кадры (ETHIF_DO).
 *  24.11.2026: DHCP-сервер H417_FreeRTOS (192.168.77.1 на прямой кабель) заменён клиентом — плата в сети роутера.
 */
#ifndef USER_NET_H_
#define USER_NET_H_

#include <stdint.h>
#include "lwip/netif.h"

#define NET_HOSTNAME        "v307"          // и имя в запросе DHCP (опция 12)
#define NET_MDNS_SERVICE    "V307"
#define NET_IP_TEXT_LEN     16

#define NET_TASK_STACK      1024            // слов
#define NET_TASK_PRIO       4               // ниже tcpip (5): принятый кадр lwIP разбирает сразу
#define NET_LINK_BPS        10000000UL      // PHY 10BASE-T
#define NET_FRAME_OVERHEAD  24              // байт на кадр сверх данных: преамбула 8, CRC 4, пауза 12

typedef struct
{
    uint32_t RandState;
    uint8_t  FL_Up;                         // 1 - стек поднят
    uint8_t  FL_Bound;                      // 1 - адрес получен по DHCP
    uint32_t Ip;                            // текущие адрес, маска, шлюз — порядок сети
    uint32_t Mask;
    uint32_t Gw;
    uint32_t BoundCnt;                      // сколько раз адрес получен (и после переподключения кабеля)
    uint32_t BoundTime;                     // последнее получение, мс от пуска
    char     IpText[NET_IP_TEXT_LEN];
    uint32_t RxBps;                         // за последнюю секунду: байт/с и кадров/с
    uint32_t TxBps;
    uint32_t RxFps;
    uint32_t TxFps;
    uint16_t LoadPm;                        // загрузка линии, промилле (по худшему направлению)
    uint32_t RxBytesLast;
    uint32_t TxBytesLast;
    uint32_t RxFramesLast;
    uint32_t TxFramesLast;
    uint32_t LoadTime;                      // прошлый расчёт, мс
} st_Net;

extern st_Net Net;

void NET_Task(void *pvParameters)               __attribute__((optimize("-Os")));
void NET_StatusCallback(struct netif *netif)    __attribute__((optimize("-Os")));
uint32_t NET_Rand(void)                         __attribute__((optimize("-Os")));
void NET_LOAD_CALC(void)                        __attribute__((optimize("-Os")));

#endif /* USER_NET_H_ */
