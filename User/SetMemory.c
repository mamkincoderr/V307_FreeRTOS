/*
 * SetMemory.c
 *
 *  Created on: 24 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Вызывается из startup_ch32v30x_D8C.S до копирования .data и обнуления .bss: глобальных переменных нет,
 *  стек временный (0x20004000).
 */
#include "ch32v30x.h"
#include "ch32v307_mem.h"

// Режим памяти: в option bytes не тот — пишем (остальные биты USER сохраняем) и сбрасываем чип.
// Записан, но не применён — нужен сброс по питанию: стоим здесь, иначе .bss выйдет за ОЗУ
void MemConfig(void)
{
    uint8_t User = *(volatile uint8_t *)MEM_OB_USER;

    if (((User >> MEM_USER_SHIFT) & 3) != CH32V307_MEM)
    {
        FLASH_Unlock();
        FLASH_ProgramOptionByteData(MEM_OB_USER, (uint8_t)((User & 0x3F) | (CH32V307_MEM << MEM_USER_SHIFT)));
        FLASH_Lock();
        NVIC_SystemReset();
    }
    while (((FLASH->OBR >> MEM_OBR_SHIFT) & 3) != CH32V307_MEM) {}
}
