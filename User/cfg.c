/*
 * cfg.c
 *
 *  Created on: 25 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Настройки FlowerGraph во флеш: две копии, запись по очереди, годность по магии и CRC32.
 */
#include <string.h>
#include "cfg.h"
#include "FreeRTOS.h"
#include "task.h"

st_Cfg Cfg;
const uint16_t CfgRateHz[CFG_RATES] = {1000, 100, 10, 1};

// CRC-32/ISO-HDLC, как zlib.crc32() в FlowerGraph: по полубайтам, таблица 64 Б
static const uint32_t CfgCrcTab[16] =
{
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
};

// Продолжение CRC: начинать с Crc = 0
uint32_t CFG_Crc32(uint32_t Crc, const void *Data, uint32_t Len)
{
    const uint8_t *p = (const uint8_t *)Data;

    Crc = ~Crc;
    while (Len--)
    {
        Crc ^= *p++;
        Crc = (Crc >> 4) ^ CfgCrcTab[Crc & 0x0F];
        Crc = (Crc >> 4) ^ CfgCrcTab[Crc & 0x0F];
    }
    return ~Crc;
}

// Запись годна: магия, CRC, значения в пределах
static uint8_t CFG_Valid(const st_CfgRec *r)
{
    uint8_t i;

    if (r->Magic != CFG_MAGIC) {return 0;}
    if (CFG_Crc32(0, r, sizeof(st_CfgRec) - 4) != r->Crc) {return 0;}
    if (r->RateIdx >= CFG_RATES) {return 0;}
    for (i = 0; i < ADCM_CHANNELS; i++)
    {
        if (r->Unit[i] > CFG_UNIT_C) {return 0;}
    }
    return 1;
}

// Из двух копий — целая и свежая; нет ни одной — по умолчанию: 1 кГц, все каналы в мВ
void CFG_Load(void)
{
    const st_CfgRec *a = (const st_CfgRec *)CFG_ADDR1;
    const st_CfgRec *b = (const st_CfgRec *)CFG_ADDR2;
    uint8_t Va = CFG_Valid(a), Vb = CFG_Valid(b);

    if (Va && (!Vb || (a->Seq >= b->Seq))) {Cfg.Rec = *a; Cfg.Loaded = 1;}
    else if (Vb) {Cfg.Rec = *b; Cfg.Loaded = 1;}
    else
    {
        memset(&Cfg.Rec, 0, sizeof(Cfg.Rec));
        Cfg.Rec.Magic = CFG_MAGIC;
        Cfg.Rec.RateIdx = CFG_RATE_DEF;
        Cfg.Loaded = 0;
    }
}

// Копия настроек для других задач (рабочая копия меняется под критической секцией)
void CFG_Get(st_CfgRec *Out, uint32_t *Gen)
{
    taskENTER_CRITICAL();
    *Out = Cfg.Rec;
    if (Gen != NULL) {*Gen = Cfg.Gen;}
    taskEXIT_CRITICAL();
}

// Новые настройки: если отличаются — во флеш (копия с меньшим номером), 1 — записано и прочитано обратно
uint8_t CFG_Save(uint8_t RateIdx, const uint8_t *Unit)
{
    static st_CfgRec New __attribute__((aligned(4)));
    const st_CfgRec *a = (const st_CfgRec *)CFG_ADDR1;
    const st_CfgRec *b = (const st_CfgRec *)CFG_ADDR2;
    uint32_t Addr;
    uint8_t i;

    if (RateIdx >= CFG_RATES) {return 0;}
    for (i = 0; i < ADCM_CHANNELS; i++)
    {
        if (Unit[i] > CFG_UNIT_C) {return 0;}
    }
    New = Cfg.Rec;
    New.RateIdx = RateIdx;
    memcpy(New.Unit, Unit, ADCM_CHANNELS);
    if (Cfg.Loaded && (memcmp(&New, &Cfg.Rec, sizeof(New)) == 0)) {return 1;}  // без изменений

    New.Magic = CFG_MAGIC;
    New.Seq = Cfg.Rec.Seq + 1;
    New.Crc = CFG_Crc32(0, &New, sizeof(New) - 4);
    if (!CFG_Valid(&New)) {return 0;}
// Пишем поверх негодной или более старой копии
    if (!CFG_Valid(a)) {Addr = CFG_ADDR1;}
    else if (!CFG_Valid(b)) {Addr = CFG_ADDR2;}
    else {Addr = (a->Seq <= b->Seq) ? CFG_ADDR1 : CFG_ADDR2;}

    __disable_irq();
    RCC->CFGR0 |= (uint32_t)RCC_HPRE_DIV4;  // как в Master16A: флеш пишем на пониженной частоте
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(Addr);
    FLASH_ProgramPage_Fast(Addr, (uint32_t *)&New);
    FLASH_Lock_Fast();
    RCC->CFGR0 &= ~(uint32_t)RCC_HPRE_DIV4;
    __enable_irq();

    Cfg.Saves++;
    if (memcmp((const void *)Addr, &New, sizeof(New)) != 0) {return 0;}
    taskENTER_CRITICAL();
    Cfg.Rec = New;
    Cfg.Loaded = 1;
    Cfg.Gen++;                              // поток FlowerGraph увидит смену схемы
    taskEXIT_CRITICAL();
    return 1;
}
