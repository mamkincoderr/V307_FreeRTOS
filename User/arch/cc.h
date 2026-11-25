/*
 * cc.h
 *
 *  Created on: 25 окт. 2026 г.
 *      Author: DeXteR
 *
 *  Платформа lwIP: RISC-V GCC, little-endian, newlib. Вывода нет — диагностика lwIP выключена,
 *  assert lwIP останавливает чип через FAULT_Assert (как configASSERT).
 */
#ifndef USER_ARCH_CC_H_
#define USER_ARCH_CC_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

void FAULT_Assert(const char *File, uint32_t Line);

#define LWIP_PLATFORM_DIAG(x)           do {} while (0)
#define LWIP_PLATFORM_ASSERT(x)         FAULT_Assert(__FILE__, __LINE__)

#endif /* USER_ARCH_CC_H_ */
