/*
 * lwipopts.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  Настройки lwIP 2.2.1: FreeRTOS, только IPv4, netconn, без сокетов.
 *  Плата в сети роутера: адрес — по DHCP (клиент lwIP). У H417_FreeRTOS был свой DHCP-сервер на прямой кабель.
 */
#ifndef USER_LWIPOPTS_H_
#define USER_LWIPOPTS_H_

#include <stdint.h>

uint32_t NET_Rand(void);

// Операционная система
#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   0
#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     0
#define LWIP_NETIF_API                  1
#define LWIP_COMPAT_MUTEX               0

// Задача tcpip: приоритет ниже таймеров FreeRTOS (7), стек в байтах
#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          4096
#define TCPIP_THREAD_PRIO               5
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_THREAD_STACKSIZE        2048
#define DEFAULT_THREAD_PRIO             3
#define DEFAULT_TCP_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_RAW_RECVMBOX_SIZE       4
#define DEFAULT_ACCEPTMBOX_SIZE         4
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_SNDTIMEO                1

// Память: своя куча lwIP и пул pbuf под приём
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        ( 16 * 1024 )     // ОЗУ 128 КБ (у H417 — 24 КБ)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                6
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define MEMP_NUM_TCP_SEG                24
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_NETCONN                12
#define MEMP_NUM_SYS_TIMEOUT            ( LWIP_NUM_SYS_TIMEOUT_INTERNAL + 6 )
#define PBUF_POOL_SIZE                  10
#define PBUF_POOL_BUFSIZE               1536

// Протоколы
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define ARP_TABLE_SIZE                  8
#define ETHARP_SUPPORT_STATIC_ENTRIES   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DHCP                       1       // клиент: адрес от роутера (с проверкой адреса ARP, ACD)
#define LWIP_AUTOIP                     0
#define LWIP_IGMP                       1       // нужен mDNS (v307.local)
#define LWIP_DNS                        0
#define LWIP_BROADCAST_PING             1

#define TCP_MSS                         1460
#define TCP_WND                         ( 4 * TCP_MSS )
#define TCP_SND_BUF                     ( 4 * TCP_MSS )
#define TCP_SND_QUEUELEN                ( ( 4 * TCP_SND_BUF ) / TCP_MSS )
#define LWIP_TCP_KEEPALIVE              1

// Интерфейс
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_EXT_STATUS_CALLBACK  1       // mDNS сам объявляет имя при появлении линка
#define LWIP_NUM_NETIF_CLIENT_DATA      1       // mDNS
#define LWIP_SINGLE_NETIF               1
#define ETH_PAD_SIZE                    0

// Контрольные суммы считает lwIP: в MAC их подсчёт выключен (как в драйвере WCH)
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_CHECK_ICMP             1

// Приложения
#define LWIP_MDNS_RESPONDER             1
#define MDNS_MAX_SERVICES               1
#define LWIP_NETBIOS_RESPOND_NAME_QUERY 1

// Статистика — на веб-страницу
#define LWIP_STATS                      1
#define LWIP_STATS_DISPLAY              0
#define LINK_STATS                      1
#define ETHARP_STATS                    1
#define IP_STATS                        1
#define ICMP_STATS                      1
#define UDP_STATS                       1
#define TCP_STATS                       1
#define MEM_STATS                       1
#define MEMP_STATS                      1
#define SYS_STATS                       1

#define LWIP_RAND()                     NET_Rand()
#define LWIP_ERRNO_STDINCLUDE           1
#define LWIP_TIMEVAL_PRIVATE            0
#define LWIP_PROVIDE_ERRNO              0

#endif /* USER_LWIPOPTS_H_ */
