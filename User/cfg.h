/*
 * cfg.h
 *
 *  Created on: 25 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Настройки потока FlowerGraph: частота выдачи и единица каждого канала (мВ или °C).
 *  Хранятся во флеш за пределами образа прошивки (последние 512 Б из 256 КБ, область без нулевого ожидания),
 *  поэтому прошивка через WCH-Link их не стирает. Две страницы по 256 Б: запись идёт в старшую по номеру
 *  копию через одну, при чтении берётся целая (магия + CRC32) с большим номером. Стёртая флеш на V307
 *  читается 0xE339E339, а не 0xFF — поэтому годность только по магии и CRC.
 *  Как в Master16A (Myflash_save.c): на время стирания и записи HCLK делится на 4.
 */
#ifndef USER_CFG_H_
#define USER_CFG_H_

#include "debug.h"
#include "adcm.h"

#define CFG_ADDR1           0x0803FE00  // две страницы быстрого стирания по 256 Б
#define CFG_ADDR2           0x0803FF00
#define CFG_PAGE            256
#define CFG_MAGIC           0x47464356  // "VCFG"

// Частота выдачи в FlowerGraph: индекс 0..3 → 1000 / 100 / 10 / 1 Гц
#define CFG_RATES           4
#define CFG_RATE_DEF        0
#define CFG_UNIT_MV         0           // канал в мВ (×10, до 3276,7 мВ)
#define CFG_UNIT_C          1           // канал в °C (×10)

typedef struct
{
    uint32_t Magic;
    uint32_t Seq;                       // номер записи: больший — свежее
    uint8_t  RateIdx;                   // CFG_RATES
    uint8_t  Unit[ADCM_CHANNELS];       // CFG_UNIT_MV / CFG_UNIT_C
    uint8_t  Res[CFG_PAGE - 4 - 4 - 1 - ADCM_CHANNELS - 4];
    uint32_t Crc;                       // CRC32 всего, что выше
} st_CfgRec;

typedef struct
{
    st_CfgRec Rec;                      // текущие настройки (рабочая копия)
    uint32_t  Saves;                    // записей во флеш с пуска
    uint8_t   Loaded;                   // 1 — прочитаны из флеш, 0 — по умолчанию
    volatile uint32_t Gen;              // счётчик смен настроек
} st_Cfg;

extern st_Cfg Cfg;
extern const uint16_t CfgRateHz[CFG_RATES];

void CFG_Get(st_CfgRec *Out, uint32_t *Gen)        __attribute__((optimize("-Os")));
void CFG_Load(void)                                 __attribute__((optimize("-Os")));
uint8_t CFG_Save(uint8_t RateIdx, const uint8_t *Unit) __attribute__((optimize("-Os")));
uint32_t CFG_Crc32(uint32_t Crc, const void *Data, uint32_t Len) __attribute__((optimize("-O2")));

#endif /* USER_CFG_H_ */
