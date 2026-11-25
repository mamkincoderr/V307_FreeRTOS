/*
 * adcm.c
 *
 *  Created on: 25 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Секвенсор крутится сам (непрерывный пуск), DMA пишет по кругу в AdcmBuf. По половине и по концу буфера
 *  прерывание DMA считает среднее блока, пропускает его через фильтр первого порядка и копит мин и макс.
 *  Задача раз в ADCM_PERIOD_MS забирает фильтр и окно,
 *  меряет Vrefint и термодатчик инжектированной группой и пересчитывает всё в вольты и градусы.
 */
#include <string.h>
#include "adcm.h"
#include "log.h"

st_Adcm Adcm;
uint32_t AdcmBuf[ADCM_BUF_WORDS] __attribute__((aligned(4)));

// Каналы, ножки, ADC1+ADC2 одновременно (регулярная и инжектированная группы), DMA по кругу, калибровка, пуск
void ADCM_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure={0};
    ADC_InitTypeDef ADC_InitStructure={0};
    DMA_InitTypeDef DMA_InitStructure={0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure={0};
    RCC_ClocksTypeDef Clk;
    uint32_t TimClk;
    uint8_t i;
    uint32_t Cal = *(volatile uint32_t *)ADCM_CAL_ADDR;

    Adcm.MeasQ = xQueueCreate(1, sizeof(st_AdcmMeas));
    Adcm.Meas.CalMv = Cal & 0xFFFF;
    Adcm.Meas.CalT = Cal >> 16;
    Adcm.Meas.Vdda = ADCM_VDDA_NOM;
    ADCM_AccReset(&Adcm.Acc);
    ADCM_FiltCoef(14000000.0f / (ADCM_PAIRS * ADCM_CONV_CYCLES));   // по номиналу, дальше по замеру
    Adcm.InjA = ADCM_Coef(1000.0f * ADCM_INJ_BLOCK / ADCM_INJ_HZ);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_ADC2 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);           // 112 / 8 = 14 МГц
    RCC_ADCCLKADJcmd(ENABLE);                   // как в кГц 3/5/8

// Входы
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_Init(GPIOC, &GPIO_InitStructure);

// DMA: 32-битное слово ADC1->RDATAR (ADC1 в младших 16, ADC2 в старших), буфер по кругу
    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)AdcmBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = ADCM_BUF_WORDS;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC, ENABLE);

// ADC1 и ADC2: регулярная + инжектированная группы одновременно, скан, непрерывно, пуск программой
    ADC_DeInit(ADC1);
    ADC_DeInit(ADC2);
    ADC_InitStructure.ADC_Mode = ADC_Mode_RegInjecSimult;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = ADCM_PAIRS;
    ADC_InitStructure.ADC_OutputBuffer = ADC_OutputBuffer_Enable;
    ADC_InitStructure.ADC_Pga = ADC_Pga_1;
    ADC_Init(ADC1, &ADC_InitStructure);
    ADC_Init(ADC2, &ADC_InitStructure);

    for (i = 0; i < ADCM_PAIRS; i++)
    {
        ADC_RegularChannelConfig(ADC1, ADC_Channel_0 + i, i + 1, ADCM_SAMPLE_TIME);     // IN0–IN5
        ADC_RegularChannelConfig(ADC2, ADC_Channel_10 + i, i + 1, ADCM_SAMPLE_TIME);    // IN10–IN15
    }
// Инжектированная группа: ADC1 — термодатчик и Vrefint; ADC2 — та же длина и выборка (каналы любые)
    ADC_InjectedSequencerLengthConfig(ADC1, 2);
    ADC_InjectedSequencerLengthConfig(ADC2, 2);
    ADC_InjectedChannelConfig(ADC1, ADC_Channel_TempSensor, 1, ADCM_INJ_SAMPLE_TIME);
    ADC_InjectedChannelConfig(ADC1, ADC_Channel_Vrefint, 2, ADCM_INJ_SAMPLE_TIME);
    ADC_InjectedChannelConfig(ADC2, ADC_Channel_10, 1, ADCM_INJ_SAMPLE_TIME);
    ADC_InjectedChannelConfig(ADC2, ADC_Channel_11, 2, ADCM_INJ_SAMPLE_TIME);
    ADC_ExternalTrigInjectedConvConfig(ADC1, ADC_ExternalTrigInjecConv_T2_TRGO);  // ADC2 идёт вместе с ADC1
    ADC_ExternalTrigInjectedConvConfig(ADC2, ADC_ExternalTrigInjecConv_None);
    ADC_ExternalTrigInjectedConvCmd(ADC1, ENABLE);
    ADC_ExternalTrigInjectedConvCmd(ADC2, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC2, ENABLE);
    ADC_TempSensorVrefintCmd(ENABLE);

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_Cmd(ADC2, ENABLE);

// Калибровка (как в кГц 3/5/8: буфер на время калибровки выключен)
    ADC_BufferCmd(ADC1, DISABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1)) {}
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1)) {}
    Adcm.Cal1 = Get_CalibrationValue(ADC1);
    ADC_BufferCmd(ADC1, ENABLE);
    ADC_BufferCmd(ADC2, DISABLE);
    ADC_ResetCalibration(ADC2);
    while (ADC_GetResetCalibrationStatus(ADC2)) {}
    ADC_StartCalibration(ADC2);
    while (ADC_GetCalibrationStatus(ADC2)) {}
    Adcm.Cal2 = Get_CalibrationValue(ADC2);
    ADC_BufferCmd(ADC2, ENABLE);

    NVIC_SetPriority(DMA1_Channel1_IRQn, ADCM_DMA_PRIO);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    ADC_ITConfig(ADC1, ADC_IT_JEOC, ENABLE);
    NVIC_SetPriority(ADC1_2_IRQn, ADCM_DMA_PRIO);
    NVIC_EnableIRQ(ADC1_2_IRQn);
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);     // дальше секвенсор крутится сам

// TIM2: TRGO по переполнению ADCM_INJ_HZ раз в секунду — пуск инжектированной группы
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_GetClocksFreq(&Clk);
    TimClk = (Clk.PCLK1_Frequency == Clk.HCLK_Frequency) ? Clk.PCLK1_Frequency : 2 * Clk.PCLK1_Frequency;
    TIM_TimeBaseStructure.TIM_Prescaler = (uint16_t)(TimClk / 1000000 - 1);     // 1 МГц
    TIM_TimeBaseStructure.TIM_Period = (uint16_t)(1000000 / ADCM_INJ_HZ - 1);
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    TIM_SelectOutputTrigger(TIM2, TIM_TRGOSource_Update);
    TIM_Cmd(TIM2, ENABLE);
}

//=====================================================================
// Задача вольтметра
//=====================================================================
// Раз в ADCM_PERIOD_MS: окно (фильтры всех каналов, мин/макс) в почтовый ящик
void ADCM_Task(void *pvParameters)
{
    TickType_t Wake = xTaskGetTickCount();

    Adcm.LastWindowTick = Wake;
    LOG_Add("ADC: 12 ch, ADCCLK %lu kHz, cal %d/%d, TS cal %u mV @ %u C, filter tau %u ms",
            (unsigned long)(SystemCoreClock / 8000), Adcm.Cal1, Adcm.Cal2, Adcm.Meas.CalMv, Adcm.Meas.CalT,
            (unsigned)ADCM_FILT_TAU_MS);
    while (1)
    {
        vTaskDelayUntil(&Wake, pdMS_TO_TICKS(ADCM_PERIOD_MS));
        ADCM_Window();
    }
}

// Коэффициент фильтра первого порядка 1 - exp(-dt/тау) в Q16; экспонента рядом до x^4 (без libm):
// x = 0,04 (каналы) — ошибка ~1e-9, x = 0,2 (Vrefint, термодатчик) — ~3e-6
int32_t ADCM_Coef(float DtMs)
{
    float X = DtMs / ADCM_FILT_TAU_MS;

    return (int32_t)(65536.0f * X * (1.0f - X * 0.5f * (1.0f - X * (1.0f / 3.0f) * (1.0f - X * 0.25f))) + 0.5f);
}

// Коэффициент фильтра каналов по скорости секвенсора: шаг = половина буфера, dt = ADCM_SWEEPS_HALF / Rate
void ADCM_FiltCoef(float Rate)
{
    Adcm.FiltA = ADCM_Coef((float)ADCM_SWEEPS_HALF * 1000.0f / Rate);
    Adcm.Meas.FiltTau = ADCM_FILT_TAU_MS;
}

// Окно: забираем фильтр и мин/макс (коротко без прерываний), считаем вольты по VDDA (Vrefint)
// и температуру NTC по доле от VDDA, публикуем результат в почтовый ящик
void ADCM_Window(void)
{
    st_AdcmMeas *M = &Adcm.Meas;
    TickType_t Now = xTaskGetTickCount();
    float K;
    uint8_t i;

    uint32_t Isr, Inj;
    int32_t F[ADCM_CHANNELS], Ts, Ref;

    taskENTER_CRITICAL();
    memcpy(F, Adcm.Filt, sizeof(F));
    Ts = Adcm.FiltTs;
    Ref = Adcm.FiltRef;
    Inj = Adcm.InjCount;
    Adcm.Snap = Adcm.Acc;
    ADCM_AccReset(&Adcm.Acc);
    Isr = Adcm.IsrCycles;
    Adcm.IsrCycles = 0;
    taskEXIT_CRITICAL();

    if (Adcm.Snap.Sweeps == 0) {return;}
    ADCM_Internal(Ts, Ref);
    K = M->Vdda / ADCM_FULL_SCALE;
    for (i = 0; i < ADCM_CHANNELS; i++)
    {
        M->U[i] = K * (float)F[i] * (1.0f / (float)(1 << ADCM_FILT_Q));
        M->Umin[i] = K * Adcm.Snap.Min[i];
        M->Umax[i] = K * Adcm.Snap.Max[i];
        M->T[i] = ADCM_NtcTemp(M->U[i] / M->Vdda);     // делитель от того же 3V3: доля = U / VDDA
    }
    if (Now != Adcm.LastWindowTick)
    {
        M->SweepRate = (Adcm.Snap.Sweeps * 1000) / (Now - Adcm.LastWindowTick);
        M->IsrLoadPm = (uint16_t)(((uint64_t)Isr * 1000) / ((uint64_t)(Now - Adcm.LastWindowTick) * (SystemCoreClock / 1000)));
        if (M->SweepRate > 1000) {ADCM_FiltCoef((float)M->SweepRate);}
        M->InjRate = ((Inj - Adcm.InjLastCount) * 1000) / (Now - Adcm.LastWindowTick);
    }
    Adcm.InjLastCount = Inj;
    Adcm.LastWindowTick = Now;
    M->Window++;
    M->Tick = Now;
    xQueueOverwrite(Adcm.MeasQ, M);
}

// Термодатчик и Vrefint из фильтров (код << ADCM_FILT_Q). VDDA = Vrefint * 4095 / код;
// температура кристалла — полиномом от отклонения напряжения датчика от заводской точки
void ADCM_Internal(int32_t Ts, int32_t Ref)
{
    st_AdcmMeas *M = &Adcm.Meas;
    const float Q = 1.0f / (float)(1 << ADCM_FILT_Q);
    float CodeTs, CodeRef, dU;

    if (Ref <= 0) {return;}         // ещё нет ни одного блока
    CodeTs = (float)Ts * Q;
    CodeRef = (float)Ref * Q;
    M->CodeTs = (uint16_t)(CodeTs + 0.5f);
    M->CodeVref = (uint16_t)(CodeRef + 0.5f);
    M->Vdda = ADCM_VREFINT_V * ADCM_FULL_SCALE / CodeRef;
    M->Vrefint = ADCM_VDDA_NOM * CodeRef / ADCM_FULL_SCALE;
    M->Uts = 1000.0f * M->Vdda * CodeTs / ADCM_FULL_SCALE;
    dU = M->Uts - (float)M->CalMv;
    M->Tcpu = (float)M->CalT + dU * (ADCM_TS_P1 + dU * (ADCM_TS_P2 + dU * ADCM_TS_P3));
}

// Температура NTC NCP18WB333J03RB по доле напряжения делителя — 1 в 1 GetTemperature() из Voltex (sys.c)
float ADCM_NtcTemp(float inUadc)
{
    float t;

    if (inUadc < NTC_X_MIN) {return T_SENS_LOW;}
    if (inUadc > NTC_X_MAX) {return T_SENS_HIGH;}
    if (inUadc < NTC_X_12)
    {   // -40...-1 C
        t = inUadc - 0.1233f;
        return (((-39494.9f*t + 4617.566f)*t - 337.0391f)*t + 155.819f)*t - 13.77888f;
    }
    if (inUadc < NTC_X_23)
    {   // -1...+69 C
        t = inUadc - 0.5369f;
        return (((287.2593f*t + 211.2645f)*t + 27.53911f)*t + 88.83231f)*t + 28.39739f;
    }
    // +69...+125 C
    t = inUadc - 0.9122f;
    return (((346340.3f*t + 29795.31f)*t + 1968.714f)*t + 370.5682f)*t + 86.0871f;
}

// Пустое накопление: мин — наибольший код, макс — ноль
void ADCM_AccReset(st_AdcmAcc *Acc)
{
    uint8_t i;

    for (i = 0; i < ADCM_CHANNELS; i++)
    {
        Acc->Min[i] = 0xFFFF;
        Acc->Max[i] = 0;
    }
    Acc->Sweeps = 0;
}

// Фильтр первого порядка: F += A * (X - F), X — среднее блока (сумма 2^LOG2 отсчётов) в кодах << ADCM_FILT_Q
static inline void ADCM_Filter(uint8_t Ch, uint32_t BlockSum)
{
    int32_t X = (int32_t)(BlockSum << (ADCM_FILT_Q - ADCM_SWEEPS_LOG2));

    if (Adcm.FiltInit == 0) {Adcm.Filt[Ch] = X; return;}
    Adcm.Filt[Ch] += (int32_t)(((int64_t)(X - Adcm.Filt[Ch]) * Adcm.FiltA) >> 16);
}

// Половина буфера: по каждой паре каналов проходим ADCM_SWEEPS_HALF кругов (шаг ADCM_PAIRS слов),
// сумма, мин и макс держатся в регистрах; сумма блока уходит в фильтр
void ADCM_Accumulate(const uint32_t *p)
{
    st_AdcmAcc *Acc = &Adcm.Acc;
    const uint32_t *q;
    uint32_t s, w, a, b, Sa, Sb, MinA, MaxA, MinB, MaxB;
    uint8_t k;

    for (k = 0; k < ADCM_PAIRS; k++)
    {
        q = p + k;
        Sa = 0; Sb = 0;
        MinA = Acc->Min[k]; MaxA = Acc->Max[k];
        MinB = Acc->Min[ADCM_PAIRS + k]; MaxB = Acc->Max[ADCM_PAIRS + k];
        for (s = 0; s < ADCM_SWEEPS_HALF; s++)
        {
            w = *q;
            q += ADCM_PAIRS;
            a = w & 0xFFFF;                 // выравнивание вправо: старшие биты каждой половины нулевые
            b = w >> 16;
            Sa += a;
            Sb += b;
            if (a < MinA) {MinA = a;}
            if (a > MaxA) {MaxA = a;}
            if (b < MinB) {MinB = b;}
            if (b > MaxB) {MaxB = b;}
        }
        Acc->Min[k] = MinA; Acc->Max[k] = MaxA;
        Acc->Min[ADCM_PAIRS + k] = MinB; Acc->Max[ADCM_PAIRS + k] = MaxB;
        ADCM_Filter(k, Sa);
        ADCM_Filter(ADCM_PAIRS + k, Sb);
        Adcm.Ring.Sum[k] += Sa;
        Adcm.Ring.Sum[ADCM_PAIRS + k] += Sb;
    }
    Adcm.Ring.Sweeps += ADCM_SWEEPS_HALF;
    Adcm.FiltInit = 1;
    Acc->Sweeps += ADCM_SWEEPS_HALF;
}

//=====================================================================
// Прерывание DMA АЦП
//=====================================================================
// Заполнена половина буфера — разбираем её, DMA тем временем пишет вторую
void DMA1_Channel1_IRQHandler(void)
{
    uint32_t Start = (uint32_t)SysTick->CNT, End;

    if (DMA_GetITStatus(DMA1_IT_HT1) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_HT1);
        ADCM_Accumulate(&AdcmBuf[0]);
        Adcm.HalfIrq++;
    }
    if (DMA_GetITStatus(DMA1_IT_TC1) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TC1);
        ADCM_Accumulate(&AdcmBuf[ADCM_BUF_WORDS / 2]);
        Adcm.FullIrq++;
    }
// Время в прерывании по SysTick (счёт вверх до CMP, затем с нуля)
    End = (uint32_t)SysTick->CNT;
    Adcm.IsrCycles += (End >= Start) ? (End - Start) : (End + (uint32_t)SysTick->CMP + 1 - Start);
}

// Миллисекунда (такт TIM2): суммы каналов за неё — в кольцо для FlowerGraph
static inline void ADCM_RingPut(void)
{
    st_AdcmRing *R = &Adcm.Ring;
    uint32_t h = R->Head;

    if ((h - R->Tail) >= ADCM_RING) {R->Lost++;}
    else
    {
        memcpy(R->Buf[h % ADCM_RING].Sum, R->Sum, sizeof(R->Sum));
        R->Buf[h % ADCM_RING].Sweeps = R->Sweeps;
        R->Head = h + 1;
    }
    memset(R->Sum, 0, sizeof(R->Sum));
    R->Sweeps = 0;
}

// Следующая миллисекунда из кольца (задача fgnet): 1 — есть
uint8_t ADCM_RingGet(st_AdcmTick *Out)
{
    st_AdcmRing *R = &Adcm.Ring;
    uint32_t t = R->Tail;

    if (t == R->Head) {return 0;}
    *Out = R->Buf[t % ADCM_RING];
    R->Tail = t + 1;
    return 1;
}

// Инжектированная группа готова (пуск от TIM2): копим блок по ADCM_INJ_BLOCK замеров, блок — в фильтры
void ADC1_2_IRQHandler(void)
{
    uint32_t Start = (uint32_t)SysTick->CNT, End;
    int32_t X;

    if (ADC_GetITStatus(ADC1, ADC_IT_JEOC) != RESET)
    {
        ADC_ClearITPendingBit(ADC1, ADC_IT_JEOC);
        Adcm.InjSumTs += ADC1->IDATAR1;
        Adcm.InjSumRef += ADC1->IDATAR2;
        Adcm.InjCount++;
        ADCM_RingPut();
        if (++Adcm.InjN >= ADCM_INJ_BLOCK)
        {
            if (Adcm.InjInit == 0)
            {
                Adcm.FiltTs = (int32_t)(Adcm.InjSumTs << (ADCM_FILT_Q - ADCM_INJ_LOG2));
                Adcm.FiltRef = (int32_t)(Adcm.InjSumRef << (ADCM_FILT_Q - ADCM_INJ_LOG2));
                Adcm.InjInit = 1;
            }
            else
            {
                X = (int32_t)(Adcm.InjSumTs << (ADCM_FILT_Q - ADCM_INJ_LOG2));
                Adcm.FiltTs += (int32_t)(((int64_t)(X - Adcm.FiltTs) * Adcm.InjA) >> 16);
                X = (int32_t)(Adcm.InjSumRef << (ADCM_FILT_Q - ADCM_INJ_LOG2));
                Adcm.FiltRef += (int32_t)(((int64_t)(X - Adcm.FiltRef) * Adcm.InjA) >> 16);
            }
            Adcm.InjSumTs = 0;
            Adcm.InjSumRef = 0;
            Adcm.InjN = 0;
        }
    }
    End = (uint32_t)SysTick->CNT;
    Adcm.IsrCycles += (End >= Start) ? (End - Start) : (End + (uint32_t)SysTick->CMP + 1 - Start);
}
