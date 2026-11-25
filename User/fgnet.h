/*
 * fgnet.h
 *
 *  Created on: 25 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  FG-NET для FlowerGraph (как у шлюза кГц, профиль STRUCTURED_V1, FlowerGraph не меняется):
 *    UDP  5000 — поток данных: заголовок 28 Б + тело + CRC32 (zlib)
 *    TCP  5001 — управление: PING/PONG, GET_STATUS, GET_SCHEMA, START_STREAM/STOP_STREAM, автостоп без PING 6 с
 *    UDP  5002 — поиск: анонс в 239.10.10.1 раз в секунду и ответ на запрос
 *  Тело пакета описывает схема (GET_SCHEMA, JSON), её собираем из настроек (cfg.h):
 *    dev   — 0 блоков (у вольтметра модулей нет)
 *    sys   — VDDA, Vrefint, кристалл, круги/с, прерывания, CPU, частота выдачи, потери
 *    scope — 12 каналов int16, множитель 0,1: мВ ×10 (потолок 3276,7 мВ) или °C ×10, по настройке канала;
 *            1000 Гц — 10 отсчётов в пакете (100 пакетов/с), 100/10/1 Гц — 1 отсчёт в пакете.
 *  Каждый отсчёт — среднее всех оцифровок за свой период (1 мс … 1 с), миллисекунды даёт кольцо АЦП.
 *  Смена настроек останавливает поток: в FlowerGraph нажать «Старт» заново (схема перечитается).
 */
#ifndef USER_FGNET_H_
#define USER_FGNET_H_

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "lwip/api.h"
#include "adcm.h"
#include "cfg.h"

#define FGNET_PORT_DATA         5000
#define FGNET_PORT_CONTROL      5001
#define FGNET_PORT_DISCOVERY    5002
#define FGNET_MCAST             "239.10.10.1"

#define FGNET_VERSION           0x01
#define FGNET_TYPE_DATA         0x01
#define FGNET_TYPE_ANNOUNCE     0x02
#define FGNET_TYPE_REQUEST      0x03
#define FGNET_FLAG_OVERFLOW     0x01
#define FGNET_FMT_STRUCTURED_V1 0x05

#define FGNET_CMD_PING          0x01
#define FGNET_CMD_PONG          0x02
#define FGNET_CMD_GET_STATUS    0x10
#define FGNET_CMD_STATUS        0x11
#define FGNET_CMD_GET_SCHEMA    0x12
#define FGNET_CMD_SCHEMA        0x13
#define FGNET_CMD_START         0x20
#define FGNET_CMD_STOP          0x21
#define FGNET_CMD_ACK           0x22

#define FGNET_HDR_SIZE          28
#define FGNET_SCOPE_N_MAX       10          // отсчётов на канал в пакете при 1000 Гц
#define FGNET_SYS_SIZE          32
#define FGNET_BODY_MAX          (FGNET_SYS_SIZE + 1 + FGNET_SCOPE_N_MAX * ADCM_CHANNELS * 2)
#define FGNET_PKT_MAX           (FGNET_HDR_SIZE + FGNET_BODY_MAX + 4)
#define FGNET_SCHEMA_SIZE       1800
#define FGNET_CTL_BUF           (3 + 64)
#define FGNET_PING_TIMEOUT_MS   6000        // без PING поток останавливается
#define FGNET_IDLE_TIMEOUT_MS   15000       // без байт — соединение управления закрываем
#define FGNET_DATA_PERIOD_MS    5           // задача данных разбирает кольцо АЦП

#define FGNET_DATA_STACK        512         // слов
#define FGNET_CTL_STACK         512
#define FGNET_DISC_STACK        384
#define FGNET_DATA_PRIO         3
#define FGNET_CTL_PRIO          2
#define FGNET_DISC_PRIO         1

#pragma pack(push, 1)
typedef struct
{
    char     Magic[4];              // "FGNT"
    uint8_t  Version;
    uint8_t  Type;
    uint8_t  Flags;
    uint32_t DeviceId;              // младшие 4 байта MAC
    uint32_t Sequence;
    uint64_t TimestampUs;
    uint8_t  ChannelCount;          // 0 (STRUCTURED_V1)
    uint8_t  SampleFormat;          // FGNET_FMT_STRUCTURED_V1
    uint8_t  SampleCount;           // 0
    uint16_t PayloadLength;
} st_FgHdr;

typedef struct
{
    float    Vdda;
    float    Vrefint;
    float    Tcpu;
    uint32_t Sweeps;                // кругов АЦП в секунду
    uint32_t IsrPm;                 // прерывания АЦП, ‰ CPU
    uint32_t Cpu;                   // задачи, %
    uint32_t RateHz;                // частота выдачи
    uint32_t Lost;                  // потери кольца АЦП
} st_FgSys;

typedef struct
{
    uint8_t  FwMajor;
    uint8_t  FwMinor;
    uint8_t  MaxChannels;
    uint32_t MaxRate;
    uint8_t  State;                 // 1 — поток идёт
    char     Name[32];
    uint32_t SchemaCrc;
} st_FgAnnounce;
#pragma pack(pop)

typedef struct
{
    volatile uint8_t  Active;       // поток идёт
    ip_addr_t DestIp;               // куда слать (адрес клиента управления)
    uint16_t  DestPort;
    volatile TickType_t LastPing;
    uint32_t  Gen;                  // настройки, по которым идёт поток
    st_CfgRec Cfg;
    uint8_t   N;                    // отсчётов на канал в пакете
    uint16_t  Div;                  // миллисекунд на отсчёт
    uint32_t  Seq;
    uint32_t  Packets;
    uint32_t  TxErr;
    uint32_t  LostSeen;
    uint32_t  SchemaCrc;
    uint32_t  SchemaGen;            // для какого Gen посчитан SchemaCrc
    uint32_t  DeviceId;
    SemaphoreHandle_t SchemaMx;     // буфер схемы общий
    char      Peer[16];             // текстом, для страницы
} st_FgNet;

extern st_FgNet FgNet;

void FGNET_Start(void)                              __attribute__((optimize("-Os")));
void FGNET_DataTask(void *pvParameters)             __attribute__((optimize("-O2")));
void FGNET_CtlTask(void *pvParameters)              __attribute__((optimize("-Os")));
void FGNET_DiscTask(void *pvParameters)             __attribute__((optimize("-Os")));
uint16_t FGNET_Schema(char *Buf, uint16_t Size, const st_CfgRec *C) __attribute__((optimize("-Os")));

#endif /* USER_FGNET_H_ */
