/*
 * fgnet.c
 *
 *  Created on: 25 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  FG-NET для FlowerGraph на lwIP netconn. Три задачи:
 *    fgdata — забирает миллисекунды из кольца АЦП, усредняет до частоты выдачи, шлёт UDP
 *    fgctl  — TCP 5001: одно соединение управления за раз
 *    fgdisc — UDP 5002: анонс в multicast раз в секунду, ответ на запрос поиска
 *  Общие поля FgNet (Active, DestIp/DestPort) меняет fgctl под критической секцией, fgdata читает копию.
 */
#include <stdio.h>
#include <string.h>
#include "fgnet.h"
#include "main.h"
#include "ethif.h"
#include "log.h"

st_FgNet FgNet;

static uint8_t  FgPkt[FGNET_PKT_MAX] __attribute__((aligned(4)));
static char     FgSchema[FGNET_SCHEMA_SIZE];
static int16_t  FgScope[ADCM_CHANNELS * FGNET_SCOPE_N_MAX];

// Номер входа АЦП по номеру канала: 0–5 → IN0–IN5, 6–11 → IN10–IN15
static uint8_t FGNET_InNum(uint8_t Ch)
{
    return (Ch < ADCM_PAIRS) ? Ch : (uint8_t)(Ch + 4);
}

// Задачи FG-NET: вызывать, когда сеть поднята (после WEB_Start)
void FGNET_Start(void)
{
    FgNet.DeviceId = (uint32_t)EthIf.Mac[2] | ((uint32_t)EthIf.Mac[3] << 8) | ((uint32_t)EthIf.Mac[4] << 16)
                   | ((uint32_t)EthIf.Mac[5] << 24);
    FgNet.SchemaMx = xSemaphoreCreateMutex();
    FgNet.SchemaGen = 0xFFFFFFFF;
    xTaskCreate(FGNET_DataTask, "fgdata", FGNET_DATA_STACK, NULL, FGNET_DATA_PRIO, NULL);
    xTaskCreate(FGNET_CtlTask, "fgctl", FGNET_CTL_STACK, NULL, FGNET_CTL_PRIO, NULL);
    xTaskCreate(FGNET_DiscTask, "fgdisc", FGNET_DISC_STACK, NULL, FGNET_DISC_PRIO, NULL);
}

//=====================================================================
// Схема тела пакета
//=====================================================================
// JSON, который FlowerGraph читает по GET_SCHEMA: раскладка тела и подписи каналов по настройкам
uint16_t FGNET_Schema(char *Buf, uint16_t Size, const st_CfgRec *C)
{
    uint8_t N = (C->RateIdx == 0) ? FGNET_SCOPE_N_MAX : 1;
    uint16_t Body = FGNET_SYS_SIZE + 1 + N * ADCM_CHANNELS * 2;
    int Len;
    uint8_t i;

    Len = snprintf(Buf, Size,
        "{\"schema\":1,\"profile\":\"STRUCTURED_V1\",\"packet\":{\"hdr\":28,\"body\":%u,\"crc\":4},\"blocks\":["
        "{\"id\":\"dev\",\"repeat\":0,\"size\":0,\"label\":\"Модуль %%d\",\"fields\":[]},"
        "{\"id\":\"sys\",\"size\":%u,\"label\":\"V307\",\"fields\":["
        "{\"k\":\"Vdda\",\"t\":\"f\",\"l\":\"VDDA\",\"u\":\"В\",\"d\":1},"
        "{\"k\":\"Vrefint\",\"t\":\"f\",\"l\":\"Vrefint\",\"u\":\"В\"},"
        "{\"k\":\"Tcpu\",\"t\":\"f\",\"l\":\"T кристалла\",\"u\":\"°C\"},"
        "{\"k\":\"Sweeps\",\"t\":\"I\",\"l\":\"АЦП, кругов/с\"},"
        "{\"k\":\"IsrPm\",\"t\":\"I\",\"l\":\"Прерывания АЦП\",\"u\":\"‰\"},"
        "{\"k\":\"Cpu\",\"t\":\"I\",\"l\":\"CPU задачи\",\"u\":\"%%\"},"
        "{\"k\":\"RateHz\",\"t\":\"I\",\"l\":\"Частота выдачи\",\"u\":\"Гц\"},"
        "{\"k\":\"Lost\",\"t\":\"I\",\"l\":\"Потери кольца АЦП\"}]},"
        "{\"id\":\"scope\",\"n\":%u,\"ch\":%u,\"scale\":0.1,\"label\":\"Входы %u Гц\",\"channels\":[",
        Body, FGNET_SYS_SIZE, N, ADCM_CHANNELS, CfgRateHz[C->RateIdx]);
    for (i = 0; (i < ADCM_CHANNELS) && (Len > 0) && (Len < Size); i++)
    {
        Len += snprintf(Buf + Len, Size - Len, "%s{\"l\":\"IN%u %s\",\"u\":\"%s\"}", (i == 0) ? "" : ",",
                        FGNET_InNum(i), (C->Unit[i] == CFG_UNIT_C) ? "T" : "U",
                        (C->Unit[i] == CFG_UNIT_C) ? "°C" : "мВ");
    }
    if ((Len > 0) && (Len < Size)) {Len += snprintf(Buf + Len, Size - Len, "]}]}");}
    if ((Len <= 0) || (Len >= Size)) {return 0;}
    return (uint16_t)Len;
}

// CRC схемы для анонса — пересчёт при смене настроек
static uint32_t FGNET_SchemaCrc(void)
{
    st_CfgRec C;
    uint32_t Gen;
    uint16_t Len;

    CFG_Get(&C, &Gen);
    if (Gen != FgNet.SchemaGen)
    {
        xSemaphoreTake(FgNet.SchemaMx, portMAX_DELAY);
        Len = FGNET_Schema(FgSchema, sizeof(FgSchema), &C);
        FgNet.SchemaCrc = CFG_Crc32(0, FgSchema, Len);
        xSemaphoreGive(FgNet.SchemaMx);
        FgNet.SchemaGen = Gen;
    }
    return FgNet.SchemaCrc;
}

// Заголовок пакета
static void FGNET_Header(uint8_t *Out, uint8_t Type, uint8_t Flags, uint16_t Payload, uint32_t Seq)
{
    st_FgHdr *h = (st_FgHdr *)Out;

    memcpy(h->Magic, "FGNT", 4);
    h->Version = FGNET_VERSION;
    h->Type = Type;
    h->Flags = Flags;
    h->DeviceId = FgNet.DeviceId;
    h->Sequence = Seq;
    h->TimestampUs = (uint64_t)xTaskGetTickCount() * 1000ULL;
    h->ChannelCount = 0;
    h->SampleFormat = FGNET_FMT_STRUCTURED_V1;
    h->SampleCount = 0;
    h->PayloadLength = Payload;
}

//=====================================================================
// Задача данных
//=====================================================================
// Отсчёт канала в единицах потока ×10: мВ по VDDA (Vrefint) или °C NTC по доле код/4095, с насыщением int16
static int16_t FGNET_Value(uint8_t Unit, float Code, float Vdda)
{
    float v;

    if (Unit == CFG_UNIT_C) {v = 10.0f * ADCM_NtcTemp(Code / ADCM_FULL_SCALE);}
    else {v = 10000.0f * Vdda * Code / ADCM_FULL_SCALE;}           // мВ ×10
    if (v > 32767.0f) {return 32767;}
    if (v < -32768.0f) {return -32768;}
    return (int16_t)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

// Пакет: sys из почтовых ящиков, scope из FgScope; CRC32 в хвост, UDP на адрес клиента
static void FGNET_Send(struct netconn *Udp, ip_addr_t *Ip, uint16_t Port)
{
    st_AdcmMeas M;
    st_SysStat S;
    st_FgSys Sys;
    struct netbuf *Nb;
    uint16_t Body = FGNET_SYS_SIZE + 1 + FgNet.N * ADCM_CHANNELS * 2, Len;
    uint32_t Crc, Lost = Adcm.Ring.Lost;
    uint8_t *p;
    void *Dst;

    memset(&M, 0, sizeof(M));
    memset(&S, 0, sizeof(S));
    xQueuePeek(Adcm.MeasQ, &M, 0);
    xQueuePeek(V307.SysQ, &S, 0);
    Sys.Vdda = M.Vdda;
    Sys.Vrefint = M.Vrefint;
    Sys.Tcpu = M.Tcpu;
    Sys.Sweeps = M.SweepRate;
    Sys.IsrPm = M.IsrLoadPm;
    Sys.Cpu = S.CpuLoad;
    Sys.RateHz = CfgRateHz[FgNet.Cfg.RateIdx];
    Sys.Lost = Lost;

    FGNET_Header(FgPkt, FGNET_TYPE_DATA, (Lost != FgNet.LostSeen) ? FGNET_FLAG_OVERFLOW : 0, Body, FgNet.Seq++);
    FgNet.LostSeen = Lost;
    p = FgPkt + FGNET_HDR_SIZE;
    memcpy(p, &Sys, sizeof(Sys));
    p += sizeof(Sys);
    *p++ = FgNet.N;
    memcpy(p, FgScope, FgNet.N * ADCM_CHANNELS * 2);
    Len = FGNET_HDR_SIZE + Body;
    Crc = CFG_Crc32(0, FgPkt, Len);
    memcpy(FgPkt + Len, &Crc, 4);
    Len += 4;

    Nb = netbuf_new();
    if (Nb == NULL) {FgNet.TxErr++; return;}
    Dst = netbuf_alloc(Nb, Len);
    if (Dst == NULL) {netbuf_delete(Nb); FgNet.TxErr++; return;}
    memcpy(Dst, FgPkt, Len);
    if (netconn_sendto(Udp, Nb, Ip, Port) == ERR_OK) {FgNet.Packets++;}
    else {FgNet.TxErr++;}
    netbuf_delete(Nb);
}

// Раз в 5 мс: миллисекунды из кольца АЦП копятся до периода выдачи (Div мс), среднее — отсчёт;
// N отсчётов — пакет. Без потока кольцо всё равно вычитываем. Смена настроек останавливает поток.
void FGNET_DataTask(void *pvParameters)
{
    static st_AdcmTick Tick;
    uint32_t Sum[ADCM_CHANNELS], Sweeps = 0, Gen;
    uint16_t Ms = 0;
    uint8_t Cnt = 0, i, Active, WasActive = 0;
    struct netconn *Udp = netconn_new(NETCONN_UDP);
    TickType_t Wake = xTaskGetTickCount();
    ip_addr_t Ip;
    uint16_t Port = 0;
    float Vdda, Ref;

    memset(Sum, 0, sizeof(Sum));
    CFG_Get(&FgNet.Cfg, &FgNet.Gen);
    while (1)
    {
        vTaskDelayUntil(&Wake, pdMS_TO_TICKS(FGNET_DATA_PERIOD_MS));
        CFG_Get(&FgNet.Cfg, &Gen);
        if (Gen != FgNet.Gen)
        {
            FgNet.Gen = Gen;
            if (FgNet.Active) {FgNet.Active = 0; LOG_Add("FG: settings changed, stream stopped");}
        }
        taskENTER_CRITICAL();
        Active = FgNet.Active;
        Ip = FgNet.DestIp;
        Port = FgNet.DestPort;
        taskEXIT_CRITICAL();
        if (Active && !WasActive)       // новый поток: с чистого листа
        {
            FgNet.N = (FgNet.Cfg.RateIdx == 0) ? FGNET_SCOPE_N_MAX : 1;
            FgNet.Div = 1000 / CfgRateHz[FgNet.Cfg.RateIdx];
            memset(Sum, 0, sizeof(Sum));
            Sweeps = 0; Ms = 0; Cnt = 0;
            FgNet.LostSeen = Adcm.Ring.Lost;
        }
        WasActive = Active;

        while (ADCM_RingGet(&Tick))
        {
            if (!Active) {continue;}
            for (i = 0; i < ADCM_CHANNELS; i++) {Sum[i] += Tick.Sum[i];}
            Sweeps += Tick.Sweeps;
            if (++Ms < FgNet.Div) {continue;}
// Отсчёт: среднее кода за период, VDDA по отфильтрованному Vrefint
            Ref = (float)Adcm.FiltRef * (1.0f / (float)(1 << ADCM_FILT_Q));
            Vdda = (Ref > 0.0f) ? (ADCM_VREFINT_V * ADCM_FULL_SCALE / Ref) : ADCM_VDDA_NOM;
            for (i = 0; i < ADCM_CHANNELS; i++)
            {
                FgScope[i * FgNet.N + Cnt] = FGNET_Value(FgNet.Cfg.Unit[i],
                        (Sweeps != 0) ? ((float)Sum[i] / (float)Sweeps) : 0.0f, Vdda);
                Sum[i] = 0;
            }
            Sweeps = 0;
            Ms = 0;
            if (++Cnt >= FgNet.N)
            {
                Cnt = 0;
                FGNET_Send(Udp, &Ip, Port);
            }
        }
    }
}

//=====================================================================
// Задача управления (TCP 5001)
//=====================================================================
// Кадр управления: CMD, LEN (LE), данные
static void FGNET_Frame(struct netconn *Conn, uint8_t Cmd, const void *Data, uint16_t Len)
{
    uint8_t h[3] = {Cmd, (uint8_t)Len, (uint8_t)(Len >> 8)};

    netconn_write(Conn, h, 3, NETCONN_COPY | ((Len != 0) ? NETCONN_MORE : 0));
    if (Len != 0) {netconn_write(Conn, Data, Len, NETCONN_COPY);}
}

static void FGNET_StreamOff(void)
{
    taskENTER_CRITICAL();
    FgNet.Active = 0;
    taskEXIT_CRITICAL();
}

// Одна команда от FlowerGraph
static void FGNET_Cmd(struct netconn *Conn, uint8_t Cmd, const uint8_t *Pl, uint16_t Len)
{
    ip_addr_t Peer;
    uint16_t PeerPort, Port, n;
    uint8_t Ack[2], St[13];
    uint32_t v;
    st_CfgRec C;

    switch (Cmd)
    {
    case FGNET_CMD_PING:
        FgNet.LastPing = xTaskGetTickCount();
        FGNET_Frame(Conn, FGNET_CMD_PONG, NULL, 0);
        break;

    case FGNET_CMD_GET_STATUS:      // uptime, потери, свободная куча, поток
        v = xTaskGetTickCount() / 1000;
        memcpy(St, &v, 4);
        v = Adcm.Ring.Lost;
        memcpy(St + 4, &v, 4);
        v = xPortGetFreeHeapSize();
        memcpy(St + 8, &v, 4);
        St[12] = FgNet.Active;
        FGNET_Frame(Conn, FGNET_CMD_STATUS, St, sizeof(St));
        break;

    case FGNET_CMD_GET_SCHEMA:
        CFG_Get(&C, NULL);
        xSemaphoreTake(FgNet.SchemaMx, portMAX_DELAY);
        n = FGNET_Schema(FgSchema, sizeof(FgSchema), &C);
        FGNET_Frame(Conn, FGNET_CMD_SCHEMA, FgSchema, n);
        xSemaphoreGive(FgNet.SchemaMx);
        break;

    case FGNET_CMD_START:           // ip:u32 port:u16 mask:u8 rate:u32 decim:u8 — адрес берём у соединения
        Port = FGNET_PORT_DATA;
        if (Len >= 6) {memcpy(&Port, Pl + 4, 2);}
        netconn_peer(Conn, &Peer, &PeerPort);
        taskENTER_CRITICAL();
        FgNet.DestIp = Peer;
        FgNet.DestPort = Port;
        FgNet.Active = 1;
        taskEXIT_CRITICAL();
        FgNet.LastPing = xTaskGetTickCount();
        ipaddr_ntoa_r(&Peer, FgNet.Peer, sizeof(FgNet.Peer));
        LOG_Add("FG: stream -> %s:%u, %u Hz", FgNet.Peer, Port, CfgRateHz[FgNet.Cfg.RateIdx]);
        Ack[0] = FGNET_CMD_START; Ack[1] = 0;
        FGNET_Frame(Conn, FGNET_CMD_ACK, Ack, 2);
        break;

    case FGNET_CMD_STOP:
        FGNET_StreamOff();
        LOG_Add("FG: stream stop");
        Ack[0] = FGNET_CMD_STOP; Ack[1] = 0;
        FGNET_Frame(Conn, FGNET_CMD_ACK, Ack, 2);
        break;

    default:
        break;
    }
}

// Соединение управления: кадры TLV, автостоп потока без PING, закрытие при тишине
static void FGNET_Conn(struct netconn *Conn)
{
    static uint8_t Acc[FGNET_CTL_BUF];
    struct netbuf *Nb;
    uint16_t Have = 0, Off, Len, n;
    TickType_t LastRx = xTaskGetTickCount();
    err_t e;

    FgNet.LastPing = LastRx;
    netconn_set_recvtimeout(Conn, 500);
    while (1)
    {
        e = netconn_recv(Conn, &Nb);
        if (e == ERR_TIMEOUT)
        {
            if (FgNet.Active && ((xTaskGetTickCount() - FgNet.LastPing) > pdMS_TO_TICKS(FGNET_PING_TIMEOUT_MS)))
            {
                FGNET_StreamOff();
                LOG_Add("FG: no ping, stream stop");
            }
            if ((xTaskGetTickCount() - LastRx) > pdMS_TO_TICKS(FGNET_IDLE_TIMEOUT_MS)) {break;}
            continue;
        }
        if (e != ERR_OK) {break;}
        LastRx = xTaskGetTickCount();
        do
        {
            n = netbuf_copy(Nb, Acc + Have, sizeof(Acc) - Have);
            Have += n;
            Off = 0;
            while ((Have - Off) >= 3)
            {
                Len = (uint16_t)Acc[Off + 1] | ((uint16_t)Acc[Off + 2] << 8);
                if (Len > (sizeof(Acc) - 3)) {Off = Have; break;}  // мусор — сброс
                if ((Have - Off) < (3 + Len)) {break;}
                FGNET_Cmd(Conn, Acc[Off], Acc + Off + 3, Len);
                Off += 3 + Len;
            }
            memmove(Acc, Acc + Off, Have - Off);
            Have -= Off;
        } while (netbuf_next(Nb) >= 0);
        netbuf_delete(Nb);
    }
    FGNET_StreamOff();              // клиента нет — поток в никуда не шлём
}

void FGNET_CtlTask(void *pvParameters)
{
    struct netconn *Listen = netconn_new(NETCONN_TCP), *Conn;

    netconn_bind(Listen, IP_ADDR_ANY, FGNET_PORT_CONTROL);
    netconn_listen(Listen);
    LOG_Add("FG: control port %u", FGNET_PORT_CONTROL);
    while (1)
    {
        if (netconn_accept(Listen, &Conn) == ERR_OK)
        {
            FGNET_Conn(Conn);
            netconn_close(Conn);
            netconn_delete(Conn);
        }
    }
}

//=====================================================================
// Задача поиска (UDP 5002)
//=====================================================================
static void FGNET_Announce(struct netconn *Udp, const ip_addr_t *Ip, uint16_t Port)
{
    static uint8_t Pkt[FGNET_HDR_SIZE + sizeof(st_FgAnnounce)];
    st_FgAnnounce *a = (st_FgAnnounce *)(Pkt + FGNET_HDR_SIZE);
    struct netbuf *Nb;
    void *Dst;

    static uint32_t Seq;

    FGNET_Header(Pkt, FGNET_TYPE_ANNOUNCE, 0, sizeof(st_FgAnnounce), Seq++);
    memset(a, 0, sizeof(*a));
    a->FwMajor = 1;
    a->FwMinor = 0;
    a->MaxChannels = ADCM_CHANNELS;
    a->MaxRate = 1000;
    a->State = FgNet.Active;
    snprintf(a->Name, sizeof(a->Name), "V307 %02X%02X%02X", EthIf.Mac[3], EthIf.Mac[4], EthIf.Mac[5]);
    a->SchemaCrc = FGNET_SchemaCrc();

    Nb = netbuf_new();
    if (Nb == NULL) {return;}
    Dst = netbuf_alloc(Nb, sizeof(Pkt));
    if (Dst != NULL)
    {
        memcpy(Dst, Pkt, sizeof(Pkt));
        netconn_sendto(Udp, Nb, Ip, Port);
    }
    netbuf_delete(Nb);
}

void FGNET_DiscTask(void *pvParameters)
{
    struct netconn *Udp = netconn_new(NETCONN_UDP);
    struct netbuf *Nb;
    ip_addr_t Grp, From;
    uint16_t FromPort;
    uint8_t Req[FGNET_HDR_SIZE];
    TickType_t Last = 0;

    ipaddr_aton(FGNET_MCAST, &Grp);
    netconn_bind(Udp, IP_ADDR_ANY, FGNET_PORT_DISCOVERY);
    netconn_join_leave_group(Udp, &Grp, IP_ADDR_ANY, NETCONN_JOIN);
    netconn_set_recvtimeout(Udp, 250);
    while (1)
    {
        if (netconn_recv(Udp, &Nb) == ERR_OK)
        {
            From = *netbuf_fromaddr(Nb);
            FromPort = netbuf_fromport(Nb);
            if ((netbuf_copy(Nb, Req, sizeof(Req)) == sizeof(Req)) && (memcmp(Req, "FGNT", 4) == 0)
                && (Req[5] == FGNET_TYPE_REQUEST))
            {
                FGNET_Announce(Udp, &From, FromPort);     // ответ спросившему
            }
            netbuf_delete(Nb);
        }
        if ((xTaskGetTickCount() - Last) >= pdMS_TO_TICKS(1000))
        {
            Last = xTaskGetTickCount();
            FGNET_Announce(Udp, &Grp, FGNET_PORT_DISCOVERY);
        }
    }
}
