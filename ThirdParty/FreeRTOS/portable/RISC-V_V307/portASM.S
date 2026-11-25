/*
 * FreeRTOS Kernel V10.4.6
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 * Порт WCH для CH32H417 (EVT V1.6, CPU/OS/FreeRTOS, ядро V5F), переделанный под аппаратный FPU.
 * Правки H417_FreeRTOS (Дмитрий, GitHub mamkincoderr), 25.10.2026:
 *   - кадр задачи: [0] mepc, [1] mstatus, [2] x1, [3..29] x5..x31 — mepc и mstatus всегда
 *     в самом нижнем кадре, как в порте FreeRTOS V11;
 *   - FPU сохраняется лениво: только если у задачи mstatus.FS = Dirty. Тогда ниже целого кадра
 *     кладётся кадр FPU ([0] mepc, [1] mstatus, [2..33] f0..f31, [34] fcsr), а по FS = Dirty
 *     в сохранённом mstatus восстановление узнаёт, что он есть;
 *   - новая задача стартует с FS = Dirty, как у WCH, и поле FS после пуска не меняется: обработчики
 *     прерываний с interrupt() при ilp32f сами сохраняют и восстанавливают FP-регистры, после чего
 *     FS у прерванной задачи всё равно Dirty — ленивость ничего бы не дала. Ядро V5F поле FS
 *     отслеживает (тест H417_FsTest, 25.10.2026), проверка Dirty здесь оставлена.
 * V307_FreeRTOS (24.11.2026): тот же файл для CH32V307 (ядро V4F) без изменений кода — кадр задачи,
 * SW_Handler и GIHWSTKNEN (0x804, бит 5) у V4F те же. Отличия порта — в port.c (SysTick V307).
 */
#if __riscv_xlen == 32
	#define store_x sw
	#define load_x lw
	#define portWORD_SIZE 4
#else
	#error Only RV32 is supported
#endif

#include "freertos_risc_v_chip_specific_extensions.h"

#if defined( __riscv_flen ) && ( __riscv_flen == 32 )
	#define portasmHAS_FPU 1
#else
	#define portasmHAS_FPU 0
#endif

/* Целый кадр: 30 слов, округлено до 16 байт. */
#define portCONTEXT_SIZE      ( 32 * portWORD_SIZE )
/* Кадр FPU: mepc, mstatus, f0..f31, fcsr — 35 слов, округлено до 16 байт. */
#define portFPU_CONTEXT_SIZE  ( 36 * portWORD_SIZE )

#define portMSTATUS_FS_SHIFT  13

.global xPortStartFirstTask
.global SW_Handler
.global pxPortInitialiseStack

.extern pxCurrentTCB
.extern vTaskSwitchContext
.extern xISRStackTop
/*-----------------------------------------------------------*/
.align 8
.func
SW_Handler:
	addi sp, sp, -portCONTEXT_SIZE
	store_x x1, 2 * portWORD_SIZE( sp )
	store_x x5, 3 * portWORD_SIZE( sp )
	store_x x6, 4 * portWORD_SIZE( sp )
	store_x x7, 5 * portWORD_SIZE( sp )
	store_x x8, 6 * portWORD_SIZE( sp )
	store_x x9, 7 * portWORD_SIZE( sp )
	store_x x10, 8 * portWORD_SIZE( sp )
	store_x x11, 9 * portWORD_SIZE( sp )
	store_x x12, 10 * portWORD_SIZE( sp )
	store_x x13, 11 * portWORD_SIZE( sp )
	store_x x14, 12 * portWORD_SIZE( sp )
	store_x x15, 13 * portWORD_SIZE( sp )
	store_x x16, 14 * portWORD_SIZE( sp )
	store_x x17, 15 * portWORD_SIZE( sp )
	store_x x18, 16 * portWORD_SIZE( sp )
	store_x x19, 17 * portWORD_SIZE( sp )
	store_x x20, 18 * portWORD_SIZE( sp )
	store_x x21, 19 * portWORD_SIZE( sp )
	store_x x22, 20 * portWORD_SIZE( sp )
	store_x x23, 21 * portWORD_SIZE( sp )
	store_x x24, 22 * portWORD_SIZE( sp )
	store_x x25, 23 * portWORD_SIZE( sp )
	store_x x26, 24 * portWORD_SIZE( sp )
	store_x x27, 25 * portWORD_SIZE( sp )
	store_x x28, 26 * portWORD_SIZE( sp )
	store_x x29, 27 * portWORD_SIZE( sp )
	store_x x30, 28 * portWORD_SIZE( sp )
	store_x x31, 29 * portWORD_SIZE( sp )

	csrr t0, mstatus					/* Required for MPIE bit. */
	csrr t1, mepc
	store_x t1, 0 * portWORD_SIZE( sp )
	store_x t0, 1 * portWORD_SIZE( sp )

#if portasmHAS_FPU
	/* FS = Dirty (0b11): задача трогала FPU — сохраняем его кадр. */
	srli t2, t0, portMSTATUS_FS_SHIFT
	andi t2, t2, 3
	addi t3, x0, 3
	bne t2, t3, 1f
	addi sp, sp, -portFPU_CONTEXT_SIZE
	store_x t1, 0 * portWORD_SIZE( sp )
	store_x t0, 1 * portWORD_SIZE( sp )
	fsw f0, 2 * portWORD_SIZE( sp )
	fsw f1, 3 * portWORD_SIZE( sp )
	fsw f2, 4 * portWORD_SIZE( sp )
	fsw f3, 5 * portWORD_SIZE( sp )
	fsw f4, 6 * portWORD_SIZE( sp )
	fsw f5, 7 * portWORD_SIZE( sp )
	fsw f6, 8 * portWORD_SIZE( sp )
	fsw f7, 9 * portWORD_SIZE( sp )
	fsw f8, 10 * portWORD_SIZE( sp )
	fsw f9, 11 * portWORD_SIZE( sp )
	fsw f10, 12 * portWORD_SIZE( sp )
	fsw f11, 13 * portWORD_SIZE( sp )
	fsw f12, 14 * portWORD_SIZE( sp )
	fsw f13, 15 * portWORD_SIZE( sp )
	fsw f14, 16 * portWORD_SIZE( sp )
	fsw f15, 17 * portWORD_SIZE( sp )
	fsw f16, 18 * portWORD_SIZE( sp )
	fsw f17, 19 * portWORD_SIZE( sp )
	fsw f18, 20 * portWORD_SIZE( sp )
	fsw f19, 21 * portWORD_SIZE( sp )
	fsw f20, 22 * portWORD_SIZE( sp )
	fsw f21, 23 * portWORD_SIZE( sp )
	fsw f22, 24 * portWORD_SIZE( sp )
	fsw f23, 25 * portWORD_SIZE( sp )
	fsw f24, 26 * portWORD_SIZE( sp )
	fsw f25, 27 * portWORD_SIZE( sp )
	fsw f26, 28 * portWORD_SIZE( sp )
	fsw f27, 29 * portWORD_SIZE( sp )
	fsw f28, 30 * portWORD_SIZE( sp )
	fsw f29, 31 * portWORD_SIZE( sp )
	fsw f30, 32 * portWORD_SIZE( sp )
	fsw f31, 33 * portWORD_SIZE( sp )
	frcsr t2
	store_x t2, 34 * portWORD_SIZE( sp )
1:
#endif

	load_x  t0, pxCurrentTCB			/* Load pxCurrentTCB. */
	store_x  sp, 0( t0 )				/* Write sp to first TCB member. */

	/* GIHWSTKNEN: mret не снимает аппаратный стек HPE — sp уже другой задачи. */
	addi a1, x0, 0x20
	csrs 0x804, a1

	load_x sp, xISRStackTop				/* Switch to ISR stack before function call. */
	jal vTaskSwitchContext

	load_x  t1, pxCurrentTCB			/* Load pxCurrentTCB. */
	load_x  sp, 0( t1 )				 	/* Read sp from first TCB member. */

	load_x t0, 0 * portWORD_SIZE( sp )
	csrw mepc, t0
	load_x t0, 1 * portWORD_SIZE( sp )

#if portasmHAS_FPU
	srli t2, t0, portMSTATUS_FS_SHIFT
	andi t2, t2, 3
	addi t3, x0, 3
	bne t2, t3, 2f
	flw f0, 2 * portWORD_SIZE( sp )
	flw f1, 3 * portWORD_SIZE( sp )
	flw f2, 4 * portWORD_SIZE( sp )
	flw f3, 5 * portWORD_SIZE( sp )
	flw f4, 6 * portWORD_SIZE( sp )
	flw f5, 7 * portWORD_SIZE( sp )
	flw f6, 8 * portWORD_SIZE( sp )
	flw f7, 9 * portWORD_SIZE( sp )
	flw f8, 10 * portWORD_SIZE( sp )
	flw f9, 11 * portWORD_SIZE( sp )
	flw f10, 12 * portWORD_SIZE( sp )
	flw f11, 13 * portWORD_SIZE( sp )
	flw f12, 14 * portWORD_SIZE( sp )
	flw f13, 15 * portWORD_SIZE( sp )
	flw f14, 16 * portWORD_SIZE( sp )
	flw f15, 17 * portWORD_SIZE( sp )
	flw f16, 18 * portWORD_SIZE( sp )
	flw f17, 19 * portWORD_SIZE( sp )
	flw f18, 20 * portWORD_SIZE( sp )
	flw f19, 21 * portWORD_SIZE( sp )
	flw f20, 22 * portWORD_SIZE( sp )
	flw f21, 23 * portWORD_SIZE( sp )
	flw f22, 24 * portWORD_SIZE( sp )
	flw f23, 25 * portWORD_SIZE( sp )
	flw f24, 26 * portWORD_SIZE( sp )
	flw f25, 27 * portWORD_SIZE( sp )
	flw f26, 28 * portWORD_SIZE( sp )
	flw f27, 29 * portWORD_SIZE( sp )
	flw f28, 30 * portWORD_SIZE( sp )
	flw f29, 31 * portWORD_SIZE( sp )
	flw f30, 32 * portWORD_SIZE( sp )
	flw f31, 33 * portWORD_SIZE( sp )
	load_x t2, 34 * portWORD_SIZE( sp )
	fscsr t2
	addi sp, sp, portFPU_CONTEXT_SIZE
2:
#endif

	/* Load mstatus with the interrupt enable bits used by the task. */
	csrw mstatus, t0						/* Required for MPIE bit. */

	load_x  x1, 2 * portWORD_SIZE( sp )
	load_x  x5, 3 * portWORD_SIZE( sp )		/* t0 */
	load_x  x6, 4 * portWORD_SIZE( sp )		/* t1 */
	load_x  x7, 5 * portWORD_SIZE( sp )		/* t2 */
	load_x  x8, 6 * portWORD_SIZE( sp )		/* s0/fp */
	load_x  x9, 7 * portWORD_SIZE( sp )		/* s1 */
	load_x  x10, 8 * portWORD_SIZE( sp )	/* a0 */
	load_x  x11, 9 * portWORD_SIZE( sp )	/* a1 */
	load_x  x12, 10 * portWORD_SIZE( sp )	/* a2 */
	load_x  x13, 11 * portWORD_SIZE( sp )	/* a3 */
	load_x  x14, 12 * portWORD_SIZE( sp )	/* a4 */
	load_x  x15, 13 * portWORD_SIZE( sp )	/* a5 */
	load_x  x16, 14 * portWORD_SIZE( sp )	/* a6 */
	load_x  x17, 15 * portWORD_SIZE( sp )	/* a7 */
	load_x  x18, 16 * portWORD_SIZE( sp )	/* s2 */
	load_x  x19, 17 * portWORD_SIZE( sp )	/* s3 */
	load_x  x20, 18 * portWORD_SIZE( sp )	/* s4 */
	load_x  x21, 19 * portWORD_SIZE( sp )	/* s5 */
	load_x  x22, 20 * portWORD_SIZE( sp )	/* s6 */
	load_x  x23, 21 * portWORD_SIZE( sp )	/* s7 */
	load_x  x24, 22 * portWORD_SIZE( sp )	/* s8 */
	load_x  x25, 23 * portWORD_SIZE( sp )	/* s9 */
	load_x  x26, 24 * portWORD_SIZE( sp )	/* s10 */
	load_x  x27, 25 * portWORD_SIZE( sp )	/* s11 */
	load_x  x28, 26 * portWORD_SIZE( sp )	/* t3 */
	load_x  x29, 27 * portWORD_SIZE( sp )	/* t4 */
	load_x  x30, 28 * portWORD_SIZE( sp )	/* t5 */
	load_x  x31, 29 * portWORD_SIZE( sp )	/* t6 */
	addi sp, sp, portCONTEXT_SIZE

	mret
	.endfunc
/*-----------------------------------------------------------*/

.align 8
.func
xPortStartFirstTask:

/* if it is an assembly entry code, the SP offset value is determined by the assembly code,
but the C code is determined by the compiler, so we subtract 512 here as a reservation.
When entering the interrupt function of C code, the compiler automatically presses the stack
into the task stack. We can only change the SP value used by the calling function after switching
the interrupt stack.This problem can be solved by modifying the interrupt to the assembly entry,
and there is no need to reserve 512 bytes. You only need to switch the interrupt stack at the
beginning of the interrupt function */
	lw t0, xISRStackTop
	addi t0, t0, -512
	csrw mscratch,t0

	load_x  sp, pxCurrentTCB			/* Load pxCurrentTCB. */
	load_x  sp, 0( sp )				 	/* Read sp from first TCB member. */

#if portasmHAS_FPU
	/* Кадр первой задачи создан pxPortInitialiseStack: в кадре FPU только fcsr = 0 — его и ставим. */
	load_x t0, 1 * portWORD_SIZE( sp )
	srli t2, t0, portMSTATUS_FS_SHIFT
	andi t2, t2, 3
	addi t3, x0, 3
	bne t2, t3, 3f
	fscsr x0
	addi sp, sp, portFPU_CONTEXT_SIZE
3:
#endif
	load_x  x1, 0 * portWORD_SIZE( sp )	/* Note for starting the scheduler the exception return address is used as the function return address. */

	load_x  x6, 4 * portWORD_SIZE( sp )		/* t1 */
	load_x  x7, 5 * portWORD_SIZE( sp )		/* t2 */
	load_x  x8, 6 * portWORD_SIZE( sp )		/* s0/fp */
	load_x  x9, 7 * portWORD_SIZE( sp )		/* s1 */
	load_x  x10, 8 * portWORD_SIZE( sp )	/* a0 */
	load_x  x11, 9 * portWORD_SIZE( sp )	/* a1 */
	load_x  x12, 10 * portWORD_SIZE( sp )	/* a2 */
	load_x  x13, 11 * portWORD_SIZE( sp )	/* a3 */
	load_x  x14, 12 * portWORD_SIZE( sp )	/* a4 */
	load_x  x15, 13 * portWORD_SIZE( sp )	/* a5 */
	load_x  x16, 14 * portWORD_SIZE( sp )	/* a6 */
	load_x  x17, 15 * portWORD_SIZE( sp )	/* a7 */
	load_x  x18, 16 * portWORD_SIZE( sp )	/* s2 */
	load_x  x19, 17 * portWORD_SIZE( sp )	/* s3 */
	load_x  x20, 18 * portWORD_SIZE( sp )	/* s4 */
	load_x  x21, 19 * portWORD_SIZE( sp )	/* s5 */
	load_x  x22, 20 * portWORD_SIZE( sp )	/* s6 */
	load_x  x23, 21 * portWORD_SIZE( sp )	/* s7 */
	load_x  x24, 22 * portWORD_SIZE( sp )	/* s8 */
	load_x  x25, 23 * portWORD_SIZE( sp )	/* s9 */
	load_x  x26, 24 * portWORD_SIZE( sp )	/* s10 */
	load_x  x27, 25 * portWORD_SIZE( sp )	/* s11 */
	load_x  x28, 26 * portWORD_SIZE( sp )	/* t3 */
	load_x  x29, 27 * portWORD_SIZE( sp )	/* t4 */
	load_x  x30, 28 * portWORD_SIZE( sp )	/* t5 */
	load_x  x31, 29 * portWORD_SIZE( sp )	/* t6 */

	load_x  x5, 1 * portWORD_SIZE( sp )		/* Initial mstatus into x5 (t0) */
	addi x5, x5, 0x08						/* Set MIE bit so the first task starts with interrupts enabled - required as returns with ret not eret. */
	csrrw  x0, mstatus, x5					/* Interrupts enabled from here! */
	load_x  x5, 3 * portWORD_SIZE( sp )		/* Initial x5 (t0) value. */

	addi	sp, sp, portCONTEXT_SIZE
	ret
	.endfunc
/*-----------------------------------------------------------*/

/*
 * StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters );
 * a0 = pxTopOfStack, a1 = pxCode, a2 = pvParameters; новая вершина стека — в a0.
 *
 * Кадр новой задачи (целый, без FPU):
 *   [0] mepc = pxCode, [1] mstatus, [2] x1 = 0, [8] x10 (a0) = pvParameters, остальное — мусор.
 * mstatus: MIE = 0, MPIE = 1, MPP = M, FS = Dirty (FPU включён, поле FS больше не меняется).
 */
.align 8
.func
pxPortInitialiseStack:

	csrr t0, mstatus					/* Obtain current mstatus value. */
	andi t0, t0, ~0x8					/* MIE = 0 */
	li t1, ~0x6000
	and t0, t0, t1						/* FS = 0 */
	li t1, 0x7880						/* FS = Dirty (0x6000), MPP = M (0x1800), MPIE (0x80) */
	or t0, t0, t1

	addi a0, a0, -portCONTEXT_SIZE
	store_x a1, 0 * portWORD_SIZE( a0 )	/* mepc = pxCode */
	store_x t0, 1 * portWORD_SIZE( a0 )	/* mstatus */
	store_x x0, 2 * portWORD_SIZE( a0 )	/* x1: задача не возвращается */
	store_x a2, 8 * portWORD_SIZE( a0 )	/* x10 (a0) = pvParameters */
#if portasmHAS_FPU
	/* FS = Dirty: восстановление ждёт кадр FPU. f0..f31 не важны, fcsr = 0 (округление к ближайшему). */
	addi a0, a0, -portFPU_CONTEXT_SIZE
	store_x a1, 0 * portWORD_SIZE( a0 )
	store_x t0, 1 * portWORD_SIZE( a0 )
	store_x x0, 34 * portWORD_SIZE( a0 )
#endif
	ret
	.endfunc
