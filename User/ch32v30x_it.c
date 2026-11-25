/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32v30x_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2024/03/05
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32v30x_it.h"
#include "main.h"

/* V307_FreeRTOS: HPE выключен (startup, 0x804 = 0x0E) — обработчики с обычным interrupt() */
void NMI_Handler(void) __attribute__((interrupt()));
void HardFault_Handler(void) __attribute__((interrupt()));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

// Исключение: причина и адрес — в Fault (RAM без обнуления), затем сброс (как H417_FreeRTOS)
void HardFault_Handler(void)
{
    __asm volatile ("csrr %0, mcause" : "=r"(Fault.Mcause));
    __asm volatile ("csrr %0, mepc" : "=r"(Fault.Mepc));
    __asm volatile ("csrr %0, mtval" : "=r"(Fault.Mtval));
    Fault.Type = FAULT_EXCEPTION;
    FAULT_Stop();
}
