/*
 * ch32v307_mem.h
 *
 *  Created on: 24 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Режим памяти CH32V307 (RAM_CODE_MOD, биты 7:6 байта USER option bytes): окно CODE + SRAM = 320 КБ.
 *  Основа — SetMemory.c шаблона CH32v3xx_Cmake. Смена режима — здесь и в Ld/Link.ld.
 */
#ifndef USER_CH32V307_MEM_H_
#define USER_CH32V307_MEM_H_

#include <stdint.h>

#define MEM192_128          0           // CODE 192K + RAM 128K
#define MEM224_96           1           // CODE 224K + RAM  96K
#define MEM256_64           2           // CODE 256K + RAM  64K
#define MEM288_32           3           // CODE 288K + RAM  32K (заводской)

#define CH32V307_MEM        MEM192_128

#define MEM_OB_USER         0x1FFFF802  // байт USER (младший байт полуслова, старший — его инверсия)
#define MEM_USER_SHIFT      6
#define MEM_OBR_SHIFT       8           // FLASH->OBR: режим, применённый при последнем сбросе

void MemConfig(void)                    __attribute__((optimize("-O0")));

#endif /* USER_CH32V307_MEM_H_ */
