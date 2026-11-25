/*
 * ethif.h
 *
 *  Created on: 24 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Ethernet 10BASE-T (MAC + встроенный PHY CH32V307) для lwIP. Основа — eth_driver_10M.c из WCH EVT ETH/MAC_RAW:
 *  настройка MAC, дескрипторы DMA, автомат согласования PHY (полярность PN, перестановка TX/RX, сброс линка).
 *  Без библиотеки WCHNET. Перенос ethif.c из H417_FreeRTOS (там PHY 10/100 и драйвер 100M).
 */
#ifndef USER_ETHIF_H_
#define USER_ETHIF_H_

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/netif.h"

#define ETH_RXBUFNB             6           // дескрипторов приёма
#define ETH_TXBUFNB             3           // дескрипторов передачи
#define ETH_PHY_ADDR            1
#define ETH_ROM_MAC             0x1FFFF7E8  // MAC = UID в ROM, байты в обратном порядке
#define ETH_TASK_PERIOD_MS      10          // опрос приёма без прерывания и автомата PHY
#define ETH_LINK_TASK_MS        50          // шаг автомата согласования PHY (PHY_LINK_TASK_PERIOD WCH)
#define ETH_LINK_RESET_MS       500         // PHY выключен после потери линка, мс
#define ETH_TX_WAIT             200000      // попыток дождаться свободного дескриптора
#define ETH_MTU                 1500
#define ETH_REV_MASK            0xF0        // ревизия кристалла в CHIPID: 0x10, 0x20 — разные обходы WCH
#define ETH_REV_20              0x20
#define ETH_RESET_LOOPS         200         // ~10 мкс циклом (Delay_Us после старта FreeRTOS нельзя)
#define ETH_PHY_IDR1            2           // регистры ID PHY
#define ETH_PHY_IDR2            3

// Регистр PHY_MDIX (0x1E): биты 1:0 — перестановка TX/RX, биты 3:2 — полярность PN
#define PHY_ANLPAR_SELECTOR_FIELD   0x1F
#define PHY_ANLPAR_SELECTOR_VALUE   0x01
#define PHY_PN_SWITCH_P         (0<<2)
#define PHY_PN_SWITCH_N         (1<<2)
#define PHY_PN_SWITCH_AUTO      (2<<2)
#define PHY_LINK_INIT           0x00
#define PHY_LINK_WAIT_SUC       (1<<7)
#define LAST_NEGO_STAT          1           // 0 - автосогласование, 1 - параллельное определение
#define TURN_PN_POLARITY        (1<<1)      // 0 - полярность P, 1 - N

// Буферы DMA: дескрипторы и кадры
typedef struct
{
    ETH_DMADESCTypeDef RxDesc[ETH_RXBUFNB] __attribute__((aligned(4)));
    ETH_DMADESCTypeDef TxDesc[ETH_TXBUFNB] __attribute__((aligned(4)));
    uint8_t RxBuf[ETH_RXBUFNB * ETH_MAX_PACKET_SIZE] __attribute__((aligned(4)));
    uint8_t TxBuf[ETH_TXBUFNB * ETH_MAX_PACKET_SIZE] __attribute__((aligned(4)));
} st_EthDma;

typedef struct
{
    uint8_t  Mac[6];
    uint8_t  FL_Link;           // 1 - линк есть (phyStatus WCH)
    uint8_t  FL_Speed100;       // всегда 0: PHY только 10 Мбит/с (поле — для страницы, как у H417)
    uint8_t  FL_FullDuplex;     // 1 - полный дуплекс
    uint8_t  FL_LinkReset;      // 1 - линк пропал, PHY выключен на ETH_LINK_RESET_MS
    uint8_t  FL_ReInitMAC;      // 1 - MAC переинициализирован, ждём линк (ревизия 0x20)
    uint8_t  FL_PolarityDetect; // 1 - проверка полярности PN по счётчикам MMC
    uint8_t  LinkStatus;        // phyLinkStatus WCH
    uint8_t  PN;                // выбранная полярность: PHY_PN_SWITCH_*
    uint8_t  LinkCnt;           // phyLinkCnt
    uint8_t  SucCnt;            // phySucCnt
    uint8_t  TRDetectStep;      // перестановка TX/RX: 0 - не начата, 1 - идёт, 2 - готова
    uint8_t  TRDetectCnt;
    uint8_t  LinkTaskPeriod;    // шаг автомата, мс (на перестановке — случайный 50..149)
    uint8_t  NegoStat;          // LAST_NEGO_STAT | TURN_PN_POLARITY (ревизия 0x20)
    uint32_t RandVal;
    uint32_t ChipRev;           // CHIPID & ETH_REV_MASK
    uint32_t LocalTime;         // время задачи, мс
    uint32_t LinkTime;          // phyLinkTime: последний шаг автомата / потеря линка, мс
    uint32_t LinkSuccTime;      // последняя проверка полярности, мс
    uint32_t NegoLinkTime;      // линк по параллельному определению, мс
    uint16_t PhyId1;
    uint16_t PhyId2;
    uint16_t Bsr;               // последние значения регистров PHY: 1, 0, 5, 0x10
    uint16_t Bcr;
    uint16_t Anlpar;
    uint16_t PhyStat;
    uint32_t LinkUpCnt;
    uint32_t PhyResets;         // PHY_LINK_RESET
    uint32_t PnFlips;           // смена полярности по ошибкам CRC
    uint32_t ReInitCnt;         // переинициализаций MAC (ревизия 0x20)
    uint32_t RxFrames;
    uint32_t TxFrames;
    uint32_t RxBytes;           // байт кадров (без CRC) — для загрузки сети
    uint32_t TxBytes;
    uint32_t RxErr;             // кадры с ошибкой по дескриптору
    uint32_t RxDrop;            // нет pbuf или lwIP не принял
    uint32_t TxBusy;            // все дескрипторы передачи заняты
    uint32_t TxNoLink;          // кадр без линка, отброшен
    uint32_t RxStall;           // DMA приёма вставал без дескрипторов
    uint32_t BusErr;            // DMA не достал до памяти (FBE)
    uint32_t IrqCnt;
    uint32_t IrqRxCnt;          // прерываний с флагом приёма R
    uint32_t IrqLinkCnt;        // прерываний PHYLINK (линк изменился)
    uint32_t WakeIrqRx;         // проснулись по прерыванию — кадр был
    uint32_t WakeIrqEmpty;      // проснулись по прерыванию — кадра нет
    uint32_t WakeTimeoutRx;     // кадр нашёлся только по таймауту опроса
    uint8_t  FL_RxFound;        // 1 - последний проход ETHIF_Rx_DO принял кадр
    volatile uint32_t IrqFlags; // флаги DMASR из прерывания
    TaskHandle_t Task;
    struct netif Netif;
} st_EthIf;

extern st_EthIf EthIf;
extern st_EthDma EthDma;
extern ETH_DMADESCTypeDef *DMATxDescToSet;     // ch32v30x_eth.c: текущие дескрипторы
extern ETH_DMADESCTypeDef *DMARxDescToGet;

void ETHIF_HW_Init(void)                                __attribute__((optimize("-Os")));
void ETHIF_RegInit(void)                                __attribute__((optimize("-Os")));
void ETHIF_IrqEnable(void)                              __attribute__((optimize("-Os")));
err_t ETHIF_NetifInit(struct netif *netif)              __attribute__((optimize("-Os")));
err_t ETHIF_Output(struct netif *netif, struct pbuf *p) __attribute__((optimize("-Ofast")));
void ETHIF_DO(void)                                     __attribute__((optimize("-Ofast")));
void ETHIF_Rx_DO(void)                                  __attribute__((optimize("-Ofast")));
void ETHIF_Nego_DO(void)                                __attribute__((optimize("-Os")));
void ETHIF_LinkProcess(void)                            __attribute__((optimize("-Os")));
void ETHIF_PNProcess(void)                              __attribute__((optimize("-Os")));
void ETHIF_RecProcess(void)                             __attribute__((optimize("-Os")));
void ETHIF_PhyLink(void)                                __attribute__((optimize("-Os")));
void ETHIF_LinkUp(void)                                 __attribute__((optimize("-Os")));
void ETHIF_LinkDown(void)                               __attribute__((optimize("-Os")));
void ETHIF_PhyOff(void)                                 __attribute__((optimize("-Os")));
void ETHIF_ReInitMAC(void)                              __attribute__((optimize("-Os")));
void ETHIF_PhyLinkReset(void)                           __attribute__((optimize("-Os")));
void ETHIF_PnSwitch(uint8_t PNMode)                     __attribute__((optimize("-Os")));
void ETHIF_TrSwitch(void)                               __attribute__((optimize("-Os")));
void ETHIF_TrReverse(void)                              __attribute__((optimize("-Os")));
void ETHIF_RestartNego(void)                            __attribute__((optimize("-Os")));
void ETHIF_WaitLoop(uint32_t Loops)                     __attribute__((optimize("-O0")));
void ETH_IRQHandler(void)                               __attribute__((interrupt()));

#endif /* USER_ETHIF_H_ */
