/*
 * ethif.c
 *
 *  Created on: 24 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Ethernet 10BASE-T для lwIP. Кадры принимает задача сети (net.c) по сигналу прерывания DMA.
 *  Линк — по прерыванию PHYLINK встроенного PHY (как ETH_PHYLink WCH), согласование PHY — автоматом WCH
 *  раз в 10 мс (WCHNET_HandlePhyNegotiation). Delay_Us WCH после старта FreeRTOS не годится:
 *  он работает на SysTick, а это тик планировщика.
 */
#include <string.h>
#include "ethif.h"
#include "net.h"
#include "log.h"
#include "lwip/etharp.h"
#include "lwip/netifapi.h"

st_EthIf EthIf;
st_EthDma EthDma;

// Ethernet до старта планировщика: тактирование, MAC, встроенный PHY 10M, дескрипторы DMA
void ETHIF_HW_Init(void)
{
    uint8_t *Rom = (uint8_t *)(ETH_ROM_MAC + 5);
    uint16_t Timeout = 10000;
    uint8_t i;

    for (i = 0; i < 6; i++)
    {
        EthIf.Mac[i] = *(Rom - i);
    }
    EthIf.ChipRev = DBGMCU_GetCHIPID() & ETH_REV_MASK;
    EthIf.RandVal = (EthIf.Mac[3]^EthIf.Mac[4]^EthIf.Mac[5]) * 214017 + 2531017;
    EthIf.LinkTaskPeriod = ETH_LINK_TASK_MS;
    EthIf.PN = PHY_PN_SWITCH_AUTO;

    Delay_Ms(100);                              // как ETH_Init WCH

// Тактирование: MAC; PHY — от PLL3 60 МГц (HSE 8 МГц / 2 * 15)
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_ETH_MAC | RCC_AHBPeriph_ETH_MAC_Tx | RCC_AHBPeriph_ETH_MAC_Rx, ENABLE);
    RCC_PLL3Cmd(DISABLE);
    RCC_PREDIV2Config(RCC_PREDIV2_Div2);
    RCC_PLL3Config(RCC_PLL3Mul_15);
    RCC_PLL3Cmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLL3RDY) == RESET) {}

    EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;      // встроенный PHY 10BASE-T
    ETH_DeInit();
    ETH_SoftwareReset();
    do
    {
        Delay_Us(10);
        Timeout--;
    } while (((ETH->DMABMR & ETH_DMABMR_SR) != 0) && (Timeout > 0));

// MAC: как ETH_RegInit WCH; групповые кадры — все (mDNS), до проверки полярности PN фильтр выключен (ETHIF_LinkUp)
    ETH->MACMIIAR = ETH_MACMIIAR_CR_Div42;      // MDC = HCLK/42 (HCLK 60..100 МГц)
    ETH->MACCR = ETH_Watchdog_Enable | ETH_Jabber_Enable | ETH_InterFrameGap_96Bit | ETH_ChecksumOffload_Disable |
                 ETH_AutomaticPadCRCStrip_Disable | ETH_LoopbackMode_Disable | ETH_Internal_Pull_Up_Res_Enable | (1<<9);
    ETH->MACFFR = ETH_ReceiveAll_Disable | ETH_SourceAddrFilter_Disable | ETH_PassControlFrames_BlockAll |
                  ETH_BroadcastFramesReception_Enable | ETH_DestinationAddrFilter_Normal | ETH_PromiscuousMode_Disable |
                  ETH_MulticastFramesFilter_None | ETH_UnicastFramesFilter_Perfect;
    ETH->MACHTHR = 0;
    ETH->MACHTLR = 0;
    ETH->MACFCR = 0;
    ETH->MACVLANTR = ETH_VLANTagComparison_16Bit;
    ETH->DMAOMR = ETH_DropTCPIPChecksumErrorFrame_Enable | ETH_TransmitStoreForward_Enable |
                  ETH_ForwardErrorFrames_Enable | ETH_ForwardUndersizedGoodFrames_Enable;

    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_BCR, PHY_Reset);
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, PHY_PN_SWITCH_AUTO);
    EthIf.PhyId1 = ETH_ReadPHYRegister(ETH_PHY_ADDR, ETH_PHY_IDR1);
    EthIf.PhyId2 = ETH_ReadPHYRegister(ETH_PHY_ADDR, ETH_PHY_IDR2);

    ETHIF_RegInit();
}

// Адрес MAC, маски счётчиков MMC, прерывания DMA, цепочки дескрипторов (и после программного сброса MAC)
void ETHIF_RegInit(void)
{
    ETH->MACA0HR = (uint32_t)((EthIf.Mac[5]<<8) | EthIf.Mac[4]);
    ETH->MACA0LR = (uint32_t)(EthIf.Mac[0] | (EthIf.Mac[1]<<8) | (EthIf.Mac[2]<<16) | (EthIf.Mac[3]<<24));

// Счётчики MMC: прерывания на половине счёта не нужны
    ETH->MMCTIMR = ETH_MMCTIMR_TGFM;
    ETH->MMCRIMR = ETH_MMCRIMR_RGUFM | ETH_MMCRIMR_RFCEM;

// Прерывания DMA: приём, нехватка дескрипторов, сводки, смена линка PHY
    ETH_DMAITConfig(ETH_DMA_IT_NIS | ETH_DMA_IT_R | ETH_DMA_IT_AIS | ETH_DMA_IT_RBU | ETH_DMA_IT_PHYLINK, ENABLE);
    ETH_DMATxDescChainInit(EthDma.TxDesc, EthDma.TxBuf, ETH_TXBUFNB);
    ETH_DMARxDescChainInit(EthDma.RxDesc, EthDma.RxBuf, ETH_RXBUFNB);
}

// Прерывание Ethernet разрешаем, когда известна задача, которую оно будит
void ETHIF_IrqEnable(void)
{
    NVIC_SetPriority(ETH_IRQn, 0x60);
    NVIC_EnableIRQ(ETH_IRQn);
}

// Описание интерфейса для lwIP (вызывается из netif_add)
err_t ETHIF_NetifInit(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->output = etharp_output;
    netif->linkoutput = ETHIF_Output;
    netif->mtu = ETH_MTU;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, EthIf.Mac, ETH_HWADDR_LEN);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    netif_set_hostname(netif, NET_HOSTNAME);
    return ERR_OK;
}

// Передача кадра: цепочку pbuf копируем в свободный дескриптор и отдаём DMA
err_t ETHIF_Output(struct netif *netif, struct pbuf *p)
{
    ETH_DMADESCTypeDef *Desc = DMATxDescToSet;
    uint32_t Wait = 0;
    uint16_t Len;

// Без линка DMA передачи не запущен: mDNS и IGMP шлют через udp_sendto_if мимо проверки линка
    if (EthIf.FL_Link == 0)
    {
        EthIf.TxNoLink++;
        return ERR_IF;
    }
    while ((Desc->Status & ETH_DMATxDesc_OWN) != 0)
    {
        Wait++;
        if (Wait > ETH_TX_WAIT)
        {
            EthIf.TxBusy++;
            return ERR_IF;
        }
    }
    Len = pbuf_copy_partial(p, (void *)Desc->Buffer1Addr, ETH_MAX_PACKET_SIZE, 0);
    Desc->ControlBufferSize = Len & ETH_DMATxDesc_TBS1;
    __asm volatile ("fence" ::: "memory");
    Desc->Status |= ETH_DMATxDesc_LS | ETH_DMATxDesc_FS | ETH_DMATxDesc_OWN;

// DMA передачи мог встать без дескрипторов — будим
    ETH->DMASR = ETH_DMASR_TBUS;
    ETH->DMATPDR = 0;
    DMATxDescToSet = (ETH_DMADESCTypeDef *)Desc->Buffer2NextDescAddr;
    EthIf.TxFrames++;
    EthIf.TxBytes += Len;
    LINK_STATS_INC(link.xmit);
    return ERR_OK;
}

// Шаг задачи сети: ждём прерывание (не дольше 10 мс), разбираем кадры, линк, автомат PHY
void ETHIF_DO(void)
{
    uint32_t Notified;

    Notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ETH_TASK_PERIOD_MS));
    EthIf.LocalTime = xTaskGetTickCount();
    ETHIF_Rx_DO();
    if (Notified != 0)
    {
        if (EthIf.FL_RxFound == 1) {EthIf.WakeIrqRx++;}
        else {EthIf.WakeIrqEmpty++;}
    }
    else if (EthIf.FL_RxFound == 1)
    {
        EthIf.WakeTimeoutRx++;
    }
    if ((EthIf.IrqFlags & ETH_DMA_IT_PHYLINK) != 0)
    {
        taskENTER_CRITICAL();
        EthIf.IrqFlags &= ~ETH_DMA_IT_PHYLINK;
        taskEXIT_CRITICAL();
        ETHIF_PhyLink();
    }
    ETHIF_Nego_DO();
    ETHIF_RecProcess();
}

// Приём: все готовые дескрипторы — в pbuf и в lwIP, дескрипторы обратно DMA
void ETHIF_Rx_DO(void)
{
    ETH_DMADESCTypeDef *Desc = DMARxDescToGet;
    struct pbuf *p;
    uint32_t Status;
    uint16_t Len;
    uint8_t Freed = 0;

    while (((Status = Desc->Status) & ETH_DMARxDesc_OWN) == 0)
    {
        if (((Status & ETH_DMARxDesc_ES) == 0) && ((Status & ETH_DMARxDesc_FS) != 0) && ((Status & ETH_DMARxDesc_LS) != 0))
        {
            Len = ((Status & ETH_DMARxDesc_FL) >> 16) - ETH_CRC;     // CRC MAC не срезает
            p = pbuf_alloc(PBUF_RAW, Len, PBUF_POOL);
            if (p != NULL)
            {
                pbuf_take(p, (void *)Desc->Buffer1Addr, Len);
                EthIf.RxBytes += Len;
                if (EthIf.Netif.input(p, &EthIf.Netif) == ERR_OK)
                {
                    EthIf.RxFrames++;
                    LINK_STATS_INC(link.recv);
                }
                else
                {
                    pbuf_free(p);
                    EthIf.RxDrop++;
                    LINK_STATS_INC(link.drop);
                }
            }
            else
            {
                EthIf.RxDrop++;
                LINK_STATS_INC(link.memerr);
            }
        }
        else
        {
            EthIf.RxErr++;
            LINK_STATS_INC(link.err);
        }
        __asm volatile ("fence" ::: "memory");
        Desc->Status = ETH_DMARxDesc_OWN;
        Desc = (ETH_DMADESCTypeDef *)Desc->Buffer2NextDescAddr;
        Freed = 1;
    }
    DMARxDescToGet = Desc;
    EthIf.FL_RxFound = Freed;

// Без свободных дескрипторов DMA приёма стоит — пускаем
    if ((EthIf.IrqFlags & ETH_DMA_FLAG_RBU) != 0)
    {
        taskENTER_CRITICAL();
        EthIf.IrqFlags &= ~ETH_DMA_FLAG_RBU;
        taskEXIT_CRITICAL();
        EthIf.RxStall++;
    }
    if (Freed == 1) {ETH->DMARPDR = 0;}
}

// Согласование PHY (WCHNET_HandlePhyNegotiation): после потери линка PHY выключен 500 мс; без линка — автомат
// полярности и перестановки TX/RX; с линком — проверка полярности по счётчикам MMC
void ETHIF_Nego_DO(void)
{
    if (EthIf.FL_LinkReset == 1)
    {
        if ((EthIf.LocalTime - EthIf.LinkTime) >= ETH_LINK_RESET_MS)
        {
            EthIf.FL_LinkReset = 0;
            EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
            ETHIF_PhyLinkReset();
        }
    }
    else if (EthIf.FL_Link == 0)
    {
// Партнёр отвечает — шаг автомата сразу (ACCELERATE_LINK_PROCESS)
        if ((EthIf.TRDetectStep < 2) && ((ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_ANLPAR) & PHY_ANLPAR_SELECTOR_FIELD) != 0))
        {
            EthIf.LinkTaskPeriod = 0;
        }
        if ((EthIf.LocalTime - EthIf.LinkTime) >= EthIf.LinkTaskPeriod)
        {
            if (EthIf.TRDetectStep == 1)
            {
                EthIf.RandVal = EthIf.RandVal * 214017 + 2531017;
                EthIf.LinkTaskPeriod = EthIf.RandVal % 100 + 50;
            }
            else
            {
                EthIf.LinkTaskPeriod = ETH_LINK_TASK_MS;
            }
            EthIf.LinkTime = EthIf.LocalTime;
            ETHIF_LinkProcess();
        }
        EthIf.FL_ReInitMAC = 0;
    }
    else
    {
        if (EthIf.FL_ReInitMAC == 1)
        {
            if ((EthIf.LocalTime - EthIf.LinkTime) >= (5 * ETH_LINK_TASK_MS))
            {
                EthIf.FL_ReInitMAC = 0;
                if ((ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_BMSR) & PHY_Linked_Status) == 0)
                {
                    ETHIF_PhyLinkReset();
                }
            }
        }
        if (EthIf.FL_PolarityDetect == 1)
        {
            if ((EthIf.LocalTime - EthIf.LinkSuccTime) >= (2 * ETH_LINK_TASK_MS))
            {
                ETHIF_PNProcess();
            }
        }
    }
}

// Шаг автомата без линка (WCHNET_LinkProcess): партнёр виден — перестановка TX/RX, затем полярность P/N;
// не виден — перебор полярности, через 5 с без партнёра — сброс PHY
void ETHIF_LinkProcess(void)
{
    uint16_t Anlpar, Bmsr;

    Anlpar = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_ANLPAR);
    Bmsr = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_BMSR);
    EthIf.Anlpar = Anlpar;
    EthIf.Bsr = Bmsr;
    if ((Anlpar & PHY_ANLPAR_SELECTOR_FIELD) != 0)
    {
        if (EthIf.TRDetectStep == 0)
        {
            EthIf.TRDetectStep = 1;
            EthIf.TRDetectCnt = 1;
            ETHIF_TrSwitch();
            EthIf.LinkTaskPeriod = EthIf.RandVal % 100 + 50;
            return;
        }
        else if (EthIf.TRDetectStep == 1)
        {
            EthIf.TRDetectStep = 2;
            EthIf.TRDetectCnt = 0;
        }
        if ((EthIf.LinkStatus & PHY_LINK_WAIT_SUC) == 0)
        {
            if (EthIf.PN == PHY_PN_SWITCH_AUTO)
            {
                ETHIF_PnSwitch(PHY_PN_SWITCH_P);
            }
            else
            {
                EthIf.LinkStatus = PHY_LINK_WAIT_SUC;
            }
        }
        else
        {
            if ((EthIf.SucCnt++ == 5) && ((Bmsr & PHY_AutoNego_Complete) == 0))
            {
                EthIf.SucCnt = 0;
                if (EthIf.PN == PHY_PN_SWITCH_N) {ETHIF_PnSwitch(PHY_PN_SWITCH_P);}
                else {ETHIF_PnSwitch(PHY_PN_SWITCH_N);}
            }
        }
        EthIf.LinkCnt = 0;
    }
    else
    {
        if (EthIf.TRDetectStep == 1)
        {
            EthIf.TRDetectCnt++;
            if (EthIf.TRDetectCnt == 8)
            {
                EthIf.TRDetectCnt = 0;
                EthIf.TRDetectStep = 0;
                ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, PHY_PN_SWITCH_AUTO);
                return;
            }
            ETHIF_TrSwitch();
            return;
        }
        if (EthIf.LinkStatus == PHY_LINK_WAIT_SUC)
        {
            if (EthIf.LinkCnt++ == 15)
            {
                EthIf.LinkCnt = 0;
                EthIf.SucCnt = 0;
                EthIf.TRDetectStep = 0;
                EthIf.LinkStatus = PHY_LINK_INIT;
                ETHIF_PnSwitch(PHY_PN_SWITCH_AUTO);
            }
        }
        else
        {
            if (EthIf.PN == PHY_PN_SWITCH_P)
            {
                if (EthIf.LinkCnt++ == 4)
                {
                    EthIf.LinkCnt = 0;
                    ETHIF_PnSwitch(PHY_PN_SWITCH_N);
                }
            }
            else if (EthIf.PN == PHY_PN_SWITCH_N)
            {
                if (EthIf.LinkCnt++ == 15)
                {
                    EthIf.LinkCnt = 0;
                    EthIf.SucCnt = 0;
                    EthIf.TRDetectStep = 0;
                    EthIf.LinkStatus = PHY_LINK_INIT;
                    ETHIF_PnSwitch(PHY_PN_SWITCH_AUTO);
                }
            }
            else
            {
                if (EthIf.LinkCnt++ == (5000 / ETH_LINK_TASK_MS))
                {
                    ETHIF_PhyLinkReset();
                }
            }
        }
    }
}

// Полярность PN после линка (WCHNET_PhyPNProcess): юникастов нет, а ошибок CRC >= 3 — полярность наоборот;
// юникаст пришёл — полярность верная, фильтр адресов MAC включаем обратно
void ETHIF_PNProcess(void)
{
    uint16_t Mdix;

    EthIf.LinkSuccTime = EthIf.LocalTime;
    if ((ETH->MMCRGUFCR == 0) && (ETH->MMCRFCECR >= 3))
    {
        Mdix = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_MDIX);
        if (((Mdix >> 2) & 1) != 0) {Mdix &= ~(3 << 2);}
        else {Mdix |= 1 << 2;}
        ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, Mdix);
        ETH->MMCCR |= ETH_MMCCR_CR;
        while ((ETH->MMCCR & ETH_MMCCR_CR) != 0) {}
        EthIf.PnFlips++;
    }
    if (ETH->MMCRGUFCR != 0)
    {
        EthIf.FL_PolarityDetect = 0;
        ETH->MACFFR &= ~(ETH_ReceiveAll_Enable | ETH_PromiscuousMode_Enable);
    }
}

// Ревизия 0x20: MAC пропускал кадры — переинициализация MAC (WCHNET_RecProcess)
void ETHIF_RecProcess(void)
{
    if ((EthIf.ChipRev == ETH_REV_20) && ((ETH->DMAMFBOCR & 0x1FFE0000) != 0))
    {
        ETHIF_ReInitMAC();
    }
}

// Линк изменился (прерывание PHYLINK, ETH_PHYLink WCH): дуплекс по партнёру и пуск MAC, или PHY выключаем
void ETHIF_PhyLink(void)
{
    uint16_t Bsr, Bcr, Anlpar;

    Bsr = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_BSR);
    Bcr = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_BCR);
    Anlpar = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_ANLPAR);
    EthIf.Bsr = Bsr;
    EthIf.Bcr = Bcr;
    EthIf.Anlpar = Anlpar;
    if ((Bsr & PHY_Linked_Status) != 0)
    {
        if ((Bcr & PHY_AutoNegotiation) != 0)
        {
            if (Anlpar == 0)
            {
// Партнёр без автосогласования (параллельное определение): полудуплекс
                if ((Bsr & PHY_AutoNego_Complete) != 0)
                {
                    EthIf.FL_FullDuplex = 0;
                    ETHIF_LinkUp();
                    if (EthIf.ChipRev == ETH_REV_20)
                    {
                        if ((EthIf.NegoStat & TURN_PN_POLARITY) != 0)
                        {
                            ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, PHY_PN_SWITCH_N);
                        }
                        else
                        {
                            ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, PHY_PN_SWITCH_P);
                            EthIf.NegoStat |= LAST_NEGO_STAT;
                        }
                        EthIf.NegoLinkTime = EthIf.LocalTime;
                    }
                }
                else
                {
                    ETHIF_PhyLinkReset();
                }
            }
            else
            {
                EthIf.NegoStat = 0;
                if ((Bsr & PHY_AutoNego_Complete) != 0)
                {
                    EthIf.PhyStat = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_STATUS);
                    if ((EthIf.PhyStat & (1<<2)) != 0)
                    {
                        EthIf.FL_FullDuplex = 1;
                    }
                    else
                    {
                        EthIf.FL_FullDuplex = ((Anlpar & PHY_ANLPAR_SELECTOR_FIELD) != PHY_ANLPAR_SELECTOR_VALUE) ? 1 : 0;
                    }
                    ETHIF_LinkUp();
                }
                else
                {
                    ETHIF_PhyOff();
                }
            }
        }
        else
        {
            EthIf.FL_FullDuplex = 0;
            ETHIF_LinkUp();
        }
    }
    else
    {
        ETHIF_PhyOff();
        if ((EthIf.ChipRev == ETH_REV_20) && ((EthIf.NegoStat & LAST_NEGO_STAT) != 0))
        {
            if ((EthIf.LocalTime - EthIf.NegoLinkTime) < 100)
            {
                EthIf.NegoLinkTime = 0;
                (void)ETH->DMAMFBOCR;               // чтение сбрасывает счётчики
                EthIf.NegoStat ^= TURN_PN_POLARITY;
            }
        }
    }
}

// Линк есть (ETH_LinkUpCfg WCH): 10 Мбит/с, дуплекс, фильтр адресов выключен до проверки полярности, пуск MAC и DMA
void ETHIF_LinkUp(void)
{
    if (EthIf.FL_FullDuplex == 1)
    {
        ETH->MACCR |= ETH_Mode_FullDuplex;
    }
    else
    {
        ETH->MACCR &= ~ETH_Mode_FullDuplex;
    }
    ETH->MACCR &= ~(ETH_Speed_100M | ETH_Speed_1000M);
    ETH->MACFFR |= (ETH_ReceiveAll_Enable | ETH_PromiscuousMode_Enable);
    ETH->MMCCR |= ETH_MMCCR_CR;
    while ((ETH->MMCCR & ETH_MMCCR_CR) != 0) {}
    EthIf.FL_PolarityDetect = 1;
    EthIf.LinkSuccTime = EthIf.LocalTime;
    EthIf.FL_Link = 1;
    EthIf.FL_Speed100 = 0;
    EthIf.LinkUpCnt++;
    ETH_Start();

    netifapi_netif_set_link_up(&EthIf.Netif);
    LOG_Add("Link up: 10M, %s duplex", (EthIf.FL_FullDuplex == 1) ? "full" : "half");
}

// Линк пропал: в lwIP
void ETHIF_LinkDown(void)
{
    EthIf.FL_Link = 0;
    netifapi_netif_set_link_down(&EthIf.Netif);
    LOG_Add("Link down");
}

// Линк пропал (ветка Link down ETH_PHYLink WCH): PHY выключаем на ETH_LINK_RESET_MS, дальше — сброс PHY
void ETHIF_PhyOff(void)
{
    EXTEN->EXTEN_CTR &= ~EXTEN_ETH_10M_EN;
    EthIf.FL_LinkReset = 1;
    EthIf.LinkTime = EthIf.LocalTime;
    if (EthIf.FL_Link == 1) {ETHIF_LinkDown();}
}

// Переинициализация MAC с сохранением настроек (ReInitMACReg WCH, только ревизия 0x20)
void ETHIF_ReInitMAC(void)
{
    uint32_t Maccr, Macmiiar, Macffr, Machthr, Machtlr, Macfcr, Macvlantr, Dmaomr;
    uint32_t Wait = 0;
    uint16_t Timeout = 10000;

    while (((ETH->DMASR & (7 << 20)) != ETH_DMA_TransmitProcess_Suspended) && (Wait < ETH_TX_WAIT))
    {
        Wait++;
    }
    ETHIF_TrReverse();
    Macmiiar = ETH->MACMIIAR;
    Maccr = ETH->MACCR;
    Macffr = ETH->MACFFR;
    Machthr = ETH->MACHTHR;
    Machtlr = ETH->MACHTLR;
    Macfcr = ETH->MACFCR;
    Macvlantr = ETH->MACVLANTR;
    Dmaomr = ETH->DMAOMR;

    ETH_SoftwareReset();
    do
    {
        ETHIF_WaitLoop(ETH_RESET_LOOPS);
        Timeout--;
    } while (((ETH->DMABMR & ETH_DMABMR_SR) != 0) && (Timeout > 0));

    ETHIF_RegInit();
    ETH->MACMIIAR = Macmiiar;
    ETH->MACCR = Maccr;
    ETH->MACFFR = Macffr;
    ETH->MACHTHR = Machthr;
    ETH->MACHTLR = Machtlr;
    ETH->MACFCR = Macfcr;
    ETH->MACVLANTR = Macvlantr;
    ETH->DMAOMR = Dmaomr;
    ETH_Start();
    ETHIF_TrReverse();
    if (EthIf.FL_Link == 0) {ETHIF_PhyLinkReset();}
    EthIf.FL_ReInitMAC = 1;
    EthIf.LinkTime = EthIf.LocalTime;
    EthIf.ReInitCnt++;
}

// Сброс PHY и автомата согласования (PHY_LINK_RESET + PHY_NEGOTIATION_PARAM_INIT WCH)
void ETHIF_PhyLinkReset(void)
{
    if (EthIf.FL_Link == 1) {ETHIF_LinkDown();}
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_BCR, PHY_Reset);
    EthIf.SucCnt = 0;
    EthIf.LinkCnt = 0;
    EthIf.TRDetectStep = 0;
    EthIf.FL_PolarityDetect = 0;
    EthIf.LinkStatus = PHY_LINK_INIT;
    EthIf.PN = PHY_PN_SWITCH_AUTO;
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, EthIf.PN);
    EthIf.PhyResets++;
}

// Полярность PN: P, N или автомат PHY (PHY_PN_SWITCH WCH), затем заново автосогласование
void ETHIF_PnSwitch(uint8_t PNMode)
{
    uint16_t Mdix;

    if (PNMode == PHY_PN_SWITCH_AUTO)
    {
        Mdix = PHY_PN_SWITCH_AUTO;
    }
    else
    {
        Mdix = (ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_MDIX) & ~(3 << 2)) | PNMode;
    }
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, Mdix);
    EthIf.PN = PNMode;
    ETHIF_RestartNego();
}

// Перестановка пар TX/RX (PHY_TR_SWITCH WCH) и заново автосогласование
void ETHIF_TrSwitch(void)
{
    uint16_t Mdix;

    Mdix = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_MDIX);
    if ((Mdix & 1) != 0)
    {
        Mdix = (Mdix & ~3) | (1 << 1);
    }
    else
    {
        Mdix = (Mdix & ~3) | (1 << 0);
    }
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, Mdix);
    ETHIF_RestartNego();
}

// Перестановка пар TX/RX при линке, без автосогласования (PHY_TR_REVERSE WCH)
void ETHIF_TrReverse(void)
{
    uint16_t Mdix;

    if (EthIf.FL_Link == 0) {return;}
    Mdix = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_MDIX);
    if ((Mdix & 1) != 0)
    {
        Mdix = (Mdix & ~3) | (1 << 1);
    }
    else
    {
        Mdix = (Mdix & ~3) | (1 << 0);
    }
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_MDIX, Mdix);
}

// Перезапуск автосогласования (PHY_RESTART_AUTONEGOTIATION WCH)
void ETHIF_RestartNego(void)
{
    uint16_t Bcr;

    Bcr = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_BCR);
    Bcr &= ~0x01;
    Bcr |= PHY_Restart_AutoNegotiation;
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_BCR, Bcr);
    Bcr = ETH_ReadPHYRegister(ETH_PHY_ADDR, PHY_BCR);
    Bcr |= 0x03 | PHY_Restart_AutoNegotiation;
    ETH_WritePHYRegister(ETH_PHY_ADDR, PHY_BCR, Bcr);
}

// Пауза циклом вместо Delay_Us (SysTick занят тиком FreeRTOS)
void ETHIF_WaitLoop(uint32_t Loops)
{
    volatile uint32_t i;

    for (i = 0; i < Loops; i++) {}
}

//=====================================================================
// Прерывание Ethernet
//=====================================================================
// Флаги DMA снимаем и копим (и PHYLINK, бит 31), кадры и линк разбирает задача сети
void ETH_IRQHandler(void)
{
    BaseType_t Woken = pdFALSE;
    uint32_t Status = ETH->DMASR;

    ETH->DMASR = Status & (0x0001FFFF | ETH_DMA_IT_PHYLINK);     // флаги событий сбрасываются записью 1
    EthIf.IrqFlags |= Status;
    EthIf.IrqCnt++;
    if ((Status & ETH_DMA_FLAG_R) != 0) {EthIf.IrqRxCnt++;}
    if ((Status & ETH_DMA_FLAG_FBE) != 0) {EthIf.BusErr++;}
    if ((Status & ETH_DMA_IT_PHYLINK) != 0) {EthIf.IrqLinkCnt++;}
    if (EthIf.Task != NULL)
    {
        vTaskNotifyGiveFromISR(EthIf.Task, &Woken);
    }
    portYIELD_FROM_ISR(Woken);
}
