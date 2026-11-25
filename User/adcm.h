/*
 * adcm.h
 *
 *  Created on: 25 нояб. 2026 г.
 *      Author: DeXteR
 *
 *  Вольтметр: 12 каналов АЦП по кругу, как в кГц 3/5/8 (ADCmulti.c) — ADC1 и ADC2 одновременно, секвенсор
 *  с непрерывным пуском, DMA по кругу в буфер, половины буфера разбирает прерывание DMA.
 *    ADC1: IN0–IN5  = PA0–PA5 (J6, A0–A5)
 *    ADC2: IN10–IN15 = PC0–PC5 (J3.31–J3.40)
 *  Внутренние каналы — отдельно, инжектированной группой с длинной выборкой (17,1 мкс по даташиту), пуск от TIM2
 *  TRGO 1 кГц без участия программы (как в кГц: АЦП крутится сам), прерывание JEOC складывает блоки по 4 замера
 *  и пропускает их через тот же фильтр первого порядка:
 *    IN16 — датчик температуры кристалла, IN17 — Vrefint (по нему считается VDDA = верх шкалы).
 *  ADCCLK = 112 МГц / 8 = 14 МГц (предел АЦП). Выборка 1,5 + 12,5 такта = 1 мкс на пару, круг 6 мкс:
 *  ~166 000 кругов/с, 2 млн выборок/с (оба АЦП) — максимум, который даёт железо.
 *  Фильтр первого порядка (тау ADCM_FILT_TAU_MS) в прерывании DMA по среднему каждой половины буфера.
 *
 *  Потоки: прерывание DMA копит Acc; задача adc — единственный владелец регистров АЦП, забирает Acc в
 *  критической секции и раз в окно кладёт готовый st_AdcmMeas в почтовый ящик MeasQ (очередь на 1, перезапись).
 *  Читатели (веб) берут копию xQueuePeek — без общих переменных и без рваных данных.
 */
#ifndef USER_ADCM_H_
#define USER_ADCM_H_

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define ADCM_PAIRS              6           // пар ADC1/ADC2 в круге секвенсора
#define ADCM_CHANNELS           12          // 0..5 — IN0–IN5, 6..11 — IN10–IN15
#define ADCM_SWEEPS_LOG2        7
#define ADCM_SWEEPS_HALF        (1 << ADCM_SWEEPS_LOG2)     // 128 кругов в половине буфера: прерывание раз в ~0,77 мс
#define ADCM_BUF_WORDS          (2 * ADCM_SWEEPS_HALF * ADCM_PAIRS)     // слово = ADC1 (мл.) + ADC2 (ст.)
#define ADCM_SAMPLE_TIME        ADC_SampleTime_1Cycles5     // минимум: на входе нужен низкий импеданс или конденсатор
#define ADCM_CONV_CYCLES        14          // 1,5 + 12,5 такта ADCCLK на преобразование
#define ADCM_FILT_TAU_MS        20.0f       // постоянная времени фильтра первого порядка
#define ADCM_FILT_Q             16          // фильтр в кодах АЦП с 16 дробными битами
#define ADCM_INJ_SAMPLE_TIME    ADC_SampleTime_239Cycles5   // термодатчик и Vrefint: не меньше 17,1 мкс
#define ADCM_INJ_HZ             1000        // пусков инжектированной группы в секунду (TIM2 TRGO), ~3,6 % времени АЦП
#define ADCM_INJ_LOG2           2
#define ADCM_INJ_BLOCK          (1 << ADCM_INJ_LOG2)        // замеров в блоке: шаг фильтра 4 мс
#define ADCM_PERIOD_MS          50          // публикация результата (страница опрашивает 10 Гц) и окно мин/макс
#define ADCM_DMA_PRIO           0x40        // выше Ethernet (0x60) и кнопки (0x80)
#define ADCM_TASK_STACK         512         // слов
#define ADCM_TASK_PRIO          3

#define ADCM_FULL_SCALE         4095.0f
#define ADCM_VREFINT_V          1.20f       // Vrefint типовое, даташит 1,17…1,23 В
#define ADCM_VDDA_NOM           3.30f       // VDDA, если Vrefint не прочитан
#define ADCM_CAL_ADDR           0x1FFFF720  // заводская точка термодатчика: мВ (мл. 16 бит), °C (ст. 16 бит)
// Температура полиномом от отклонения напряжения датчика dU = U - Uкалибр (мВ):
//   T = Tкалибр + P1*dU + P2*dU^2 + P3*dU^3;  P1 = -1/Avg_Slope (4,3 мВ/°C по даташиту, как у WCH)
#define ADCM_TS_P1              (-1.0f / 4.3f)
#define ADCM_TS_P2              0.0f
#define ADCM_TS_P3              0.0f

// Термометр на каждом канале — как в Voltex (sys.c GetTemperature): NTC Murata NCP18WB333J03RB (33 кОм,
// B25/85 = 4108 K), NTC к +3V3, 33 кОм к GND; inUadc = U / VDDA = 33к/(33к + Rntc): делитель питается от того же 3V3, что и VDDA, а VDDA меряем по Vrefint.
// Три участка, на каждом полином 4-й степени от (inUadc - центр участка); погрешность таблицы Murata <= 0,17 C.
#define NTC_X_MIN               (0.024474f) // -41 C: ниже — обрыв
#define NTC_X_MAX               (0.971726f) // +126 C: выше — замыкание
#define NTC_X_12                (0.2204f)   // граница участков 1/2, около -1 C
#define NTC_X_23                (0.8534f)   // граница участков 2/3, около +69 C
#define T_SENS_LOW              (-50.0f)    // обрыв датчика
#define T_SENS_HIGH             (150.0f)    // замыкание датчика

// Накопление за окно: пишет прерывание DMA
typedef struct
{
    uint16_t Min[ADCM_CHANNELS];
    uint16_t Max[ADCM_CHANNELS];
    uint32_t Sweeps;                // кругов секвенсора в накоплении
} st_AdcmAcc;

// Результат окна — то, что уходит в почтовый ящик
typedef struct
{
    float    U[ADCM_CHANNELS];      // после фильтра первого порядка (тау 20 мс), В
    float    Umin[ADCM_CHANNELS];   // мин/макс отдельных отсчётов за окно, В
    float    Umax[ADCM_CHANNELS];
    float    T[ADCM_CHANNELS];      // температура NTC по U канала, C (T_SENS_LOW/HIGH — обрыв/замыкание)
    float    Vdda;                  // верх шкалы по Vrefint, В
    float    Vrefint;               // Vrefint по шкале VDDA_NOM (проверка опоры), В
    float    Tcpu;                  // температура кристалла, °C
    float    Uts;                   // напряжение термодатчика, мВ
    uint16_t CodeVref;              // средний код Vrefint
    uint16_t CodeTs;                // средний код термодатчика
    uint16_t CalMv;                 // заводская точка термодатчика, мВ
    uint16_t CalT;                  // и её температура, °C
    uint32_t SweepRate;             // кругов в секунду (каждый круг — 12 каналов)
    uint32_t Window;                // номер окна
    uint32_t Tick;                  // время окна, мс от пуска
    uint32_t InjRate;               // замеров Vrefint/термодатчика в секунду
    uint16_t IsrLoadPm;             // доля времени в прерывании DMA за окно, промилле
    float    FiltTau;               // постоянная времени фильтра, мс
} st_AdcmMeas;

typedef struct
{
    st_AdcmAcc Acc;                 // копится в прерывании
    st_AdcmAcc Snap;                // окно, взятое задачей
    st_AdcmMeas Meas;               // рабочая копия задачи adc
    int32_t  Filt[ADCM_CHANNELS];   // состояние фильтра: код АЦП << ADCM_FILT_Q (пишет прерывание)
    int32_t  FiltA;                 // коэффициент фильтра 1 - exp(-dt/тау), Q16 (пишет задача по замеру скорости)
    uint8_t  FiltInit;              // первый блок задаёт начальное состояние
    int32_t  FiltTs;                // фильтр термодатчика и Vrefint, код << ADCM_FILT_Q (пишет прерывание JEOC)
    int32_t  FiltRef;
    int32_t  InjA;                  // коэффициент фильтра для шага ADCM_INJ_BLOCK / ADCM_INJ_HZ, Q16
    uint32_t InjSumTs;              // блок, который копится
    uint32_t InjSumRef;
    uint32_t InjCount;              // замеров всего
    uint32_t InjLastCount;
    uint8_t  InjN;
    uint8_t  InjInit;
    QueueHandle_t MeasQ;            // почтовый ящик: последнее окно
    uint32_t HalfIrq;               // прерываний половины буфера
    uint32_t FullIrq;               // прерываний конца буфера
    uint32_t IsrCycles;             // тактов HCLK в прерывании DMA с прошлого окна (по SysTick)
    uint32_t LastWindowTick;
    int16_t  Cal1;                  // калибровка ADC1, ADC2
    int16_t  Cal2;
} st_Adcm;

extern st_Adcm Adcm;
extern uint32_t AdcmBuf[ADCM_BUF_WORDS];

void ADCM_Init(void)                                __attribute__((optimize("-Os")));
void ADCM_Task(void *pvParameters)                  __attribute__((optimize("-Os")));
void ADCM_Window(void)                              __attribute__((optimize("-Ofast")));
void ADCM_Internal(int32_t Ts, int32_t Ref)         __attribute__((optimize("-Ofast")));
int32_t ADCM_Coef(float DtMs)                      __attribute__((optimize("-Os")));
void ADCM_AccReset(st_AdcmAcc *Acc)                 __attribute__((optimize("-Ofast")));
float ADCM_NtcTemp(float inUadc)                    __attribute__((optimize("-Ofast")));
void ADCM_Accumulate(const uint32_t *p)             __attribute__((optimize("-Ofast")));
void ADCM_FiltCoef(float Rate)                      __attribute__((optimize("-Os")));
void DMA1_Channel1_IRQHandler(void)                 __attribute__((interrupt()));
void ADC1_2_IRQHandler(void)                        __attribute__((interrupt()));

#endif /* USER_ADCM_H_ */
