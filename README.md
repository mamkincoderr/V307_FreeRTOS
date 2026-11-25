# V307_FreeRTOS

[![visits](https://hits.sh/github.com/mamkincoderr/V307_FreeRTOS.svg?view=today-total&label=visits)](https://hits.sh/github.com/mamkincoderr/V307_FreeRTOS/)
[![stars](https://img.shields.io/github/stars/mamkincoderr/V307_FreeRTOS?logo=github)](https://github.com/mamkincoderr/V307_FreeRTOS/stargazers)
[![forks](https://img.shields.io/github/forks/mamkincoderr/V307_FreeRTOS?logo=github)](https://github.com/mamkincoderr/V307_FreeRTOS/network/members)
[![last commit](https://img.shields.io/github/last-commit/mamkincoderr/V307_FreeRTOS)](https://github.com/mamkincoderr/V307_FreeRTOS/commits/main)
[![code size](https://img.shields.io/github/languages/code-size/mamkincoderr/V307_FreeRTOS)](https://github.com/mamkincoderr/V307_FreeRTOS)
[![license](https://img.shields.io/badge/license-0BSD-blue)](LICENSE)
![MCU](https://img.shields.io/badge/MCU-CH32V307VCT6%20(V4F)-A2191F)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%2011.1-2E7D32)
![net](https://img.shields.io/badge/net-lwIP%202.2.1-6A1B9A)
![FlowerGraph](https://img.shields.io/badge/FlowerGraph-FG--NET-0A7D8C)
![build](https://img.shields.io/badge/build-CMake%20%2B%20Ninja%20%2B%20GCC%2015-064F8C)

**Вольтметр на 12 входов и NTC-термометр по Ethernet** для оценочной платы
`CH32V307V-R1-1v0`: FreeRTOS 11.1.0, lwIP 2.2.1, CMake, Ninja и WCH RISC-V GCC 15.

Прошивка запускает АЦП1 и АЦП2 параллельно, корректирует шкалу по внутреннему
Vrefint, вычисляет температуру подключённых NTC и отдаёт данные в локальную
веб-панель с частотой 10 Гц, а в [FlowerGraph](https://github.com/mamkincoderr/FlowerGraph) —
поток 12 каналов до 1 кГц. Адрес получает от DHCP-роутера; встроенного DHCP-сервера в проекте нет.

[English](README_EN.md) · [中文](README_ZH.md) · [Сборка](docs/BUILDING.md) · [Датчики и распиновка](docs/THERMISTORS.md) · [FlowerGraph](docs/FLOWERGRAPH.md)

![Веб-панель V307: снимок с живой платы](docs/images/dashboard.png)

*Снимок с живой платы 25.11.2026. Входы ещё не подключены к датчикам — напряжения и температуры на нём от наводок.*

## Проект в двух словах

| Узел | Реализация |
|---|---|
| Плата | WCH `CH32V307V-R1-1v0`, MCU `CH32V307VCT6` |
| Ядро | QingKe V4F, RISC-V, 112 МГц |
| Память | 192 КБ Flash + 128 КБ RAM |
| Аналоговые входы | 12 внешних: ADC1 `PA0–PA5` и ADC2 `PC0–PC5` |
| Внутренние каналы | IN16 — температура кристалла; IN17 — Vrefint для оценки VDDA |
| Сеть | Встроенный 10BASE-T PHY, DHCP-клиент, HTTP, mDNS и NetBIOS |
| Интерфейс | Встроенная в прошивку страница, `/api/meas`, опрос 10 Гц |
| Телеметрия | FG-NET для FlowerGraph: 12 каналов, 1000 / 100 / 10 / 1 Гц, мВ или °C на канал |
| Инструменты | Windows, CMake ≥ 3.20, Ninja, WCH GCC 15 и OpenOCD из MounRiver Studio 2 |

```bat
build.bat          :: собрать ELF, HEX и BIN в obj\
build.bat clean    :: удалить каталог сборки
build.bat flash    :: собрать, записать HEX через WCH-Link и проверить
```

Для чистой Windows-машины, установки инструментов, переменных среды и сборки в
MounRiver Studio см. [подробную инструкцию](docs/BUILDING.md). Команда `flash`
изменяет содержимое подключённого микроконтроллера.

## Подключение

1. Подайте питание и подключите отладку кабелем USB-C к разъёму P9 со встроенным
   WCH-Link.
2. Подключите RJ45 платы к локальной сети, в которой работает DHCP-роутер.
3. Подключите компьютер к той же сети. Откройте `http://v307.local` или
   `http://v307`; если mDNS/NetBIOS не разрешается, используйте адрес из DHCP-
   списка роутера.
4. Подключите датчики к входам согласно [схеме NTC и таблице контактов](docs/THERMISTORS.md).

![Распиновка CH32V307V-R1-1v0: доступные каналы АЦП, VDDA и GND](docs/pinout_adc_free.png)

## Что делает прошивка

- Одновременное сканирование 12 каналов двумя АЦП и кольцевым DMA: ~160 000 кругов/с.
- Vrefint и термодатчик кристалла — инжектированной группой, которую 1000 раз в секунду запускает TIM2.
- Фильтр первого порядка с `τ ≈ 20 мс`; публикация окна измерений каждые 50 мс.
- Пересчёт каналов в вольты по VDDA, оцениваемому через внутренний Vrefint.
- Пересчёт каждого внешнего входа в температуру NTC `Murata NCP18WB333J03RB`.
- Оценка температуры кристалла по заводской точке калибровки IN16.
- Веб-панель показывает напряжение, температуру, минимум/максимум за окно,
  состояние сети, нагрузку CPU и свободную кучу FreeRTOS.
- Встроенный HTTP-сервер выдаёт `/api/meas`; страница обновляет её 10 раз в секунду.
- lwIP предоставляет DHCP-клиент, mDNS (`v307.local`) и NetBIOS (`v307`).
- Поток в FlowerGraph по FG-NET; частота и единицы каналов задаются на странице и хранятся во флеш.

Текущая прошивка трактует все 12 внешних входов как термисторные делители. Если
подать на канал произвольный аналоговый сигнал, напряжение останется измеряемым,
но показание температуры для него не имеет смысла.

## FlowerGraph

Плата отдаёт 12 каналов в [FlowerGraph](https://github.com/mamkincoderr/FlowerGraph) по протоколу FG-NET,
FlowerGraph при этом не меняется: каналы, единицы и частоту он узнаёт из схемы, которую присылает плата.
На странице, в разделе «Настройки FlowerGraph», выбирается частота выдачи (1000 / 100 / 10 / 1 Гц)
и для каждого канала — мВ или °C. Каждый отсчёт — среднее всех оцифровок за свой период.

![Настройки FlowerGraph на странице платы](docs/images/flowergraph-settings.png)

Настройки хранятся во флеш и переживают сброс и выключение. Прошивка через WCH-Link стирает весь чип —
после неё настройки нужно выставить заново. Подробности, протокол и проверка —
в [руководстве FlowerGraph](docs/FLOWERGRAPH.md).

## Как устроено

```mermaid
flowchart LR
    ADC["ADC1 + ADC2<br/>12 каналов<br/>~160 000 кругов/с"] -->|DMA по кругу| ISR1["Прерывание DMA<br/>блок 32 круга"]
    TIM["TIM2 1 кГц"] -->|пуск| INJ["Vrefint +<br/>термодатчик"]
    INJ --> ISR2["Прерывание JEOC<br/>раз в 1 мс"]
    ISR1 -->|суммы кодов| RING["Кольцо<br/>64 × 1 мс"]
    ISR2 -->|закрыть миллисекунду| RING
    ISR1 -->|фильтр τ 20 мс| FILT["Фильтры<br/>каналов и опоры"]
    ISR2 -->|фильтр τ 20 мс| FILT
    FILT --> ADCT["Задача adc<br/>раз в 50 мс"]
    ADCT -->|xQueueOverwrite| MB["Почтовый ящик"]
    MB -->|xQueuePeek| WEB["Задача web<br/>страница 10 Гц"]
    RING --> FGD["Задача fgdata<br/>среднее за период"]
    CFG["Настройки<br/>во флеш"] --> FGD
    FGD -->|UDP 5000| FG["FlowerGraph"]
```

Прерывания только копят: суммы, минимумы и максимумы, фильтры первого порядка и кольцо миллисекунд.
Пересчёт в вольты и градусы — в задачах. Задачи не делят переменных: странице результат отдаётся через
почтовый ящик (очередь FreeRTOS на один элемент), FlowerGraph — через кольцо.

## Подключение термодатчика

На каждый канал нужен NTC `NCP18WB333J03RB` (33 кОм при 25 °C, B25/85 = 4108 K)
и постоянный резистор 33 кОм. NTC подключается к +3,3 В, постоянный резистор — к
GND, а точка между ними — к одному входу АЦП. Питание делителя должно быть тем же
3,3-вольтовым питанием, что и VDDA: прошивка вычисляет температуру по отношению
`Uвх / VDDA`.

![Подключение одного NTC к входу АЦП](docs/images/ntc-divider.png)

Подробности, назначение всех 12 входов, рекомендуемый конденсатор и ограничения
по входному напряжению собраны в [руководстве по датчикам](docs/THERMISTORS.md).

## Состояние и ограничения

На плате проверены загрузка по Ethernet, DHCP-клиент и веб-панель. Запись
измерений 25 ноября: ~160 000 кругов/с (1,92 млн выборок/с по 12 каналам), прерывания АЦП
занимают ~23 % процессора, задачи — 5–7 %. Поток FlowerGraph проверен на 1000, 100 и 1 Гц
без потерь пакетов.
Показания реальных датчиков и точность каналов под известным напряжением ещё
нужно проверить на собранном делителе и эталонном мультиметре.

Для максимальной скорости АЦП используется время выборки 1,5 такта. Источник
должен иметь низкое выходное сопротивление; для термисторного делителя рядом с
входом рекомендуется конденсатор 10–100 нФ на GND. Ethernet работает на 112 МГц;
частоту MDC и устойчивость связи следует учитывать при переносе прошивки на
другую ревизию платы.

## Исходный код и лицензии

Прикладной код проекта распространяется по [0BSD](LICENSE). Сторонние компоненты
сохраняют собственные лицензии и условия: FreeRTOS — MIT, lwIP — BSD-3-Clause,
библиотека периферии и EVT-файлы WCH — только для микроконтроллеров WCH. Полный
перечень см. в [LICENSE](LICENSE) и заголовках исходных файлов.

## Поисковые теги

`#CH32V307` `#CH32V307VCT6` `#CH32V307V-R1-1v0` `#WCH` `#QingKeV4F`
`#RISC-V` `#FreeRTOS` `#lwIP` `#Ethernet` `#ADC` `#NTC` `#NCP18WB333J03RB`
`#MounRiver` `#CMake` `#Ninja` `#WCHLink` `#FlowerGraph` `#FG-NET` `#Telemetry`
`#DataAcquisition` `#Voltmeter` `#Thermometer`

GitHub Topics этого репозитория дополнительно индексируют плату, MCU и стек:
`ch32v307`, `ch32v307vct6`, `ch32v307v-r1`, `wch`, `qingke`, `riscv`,
`freertos`, `lwip`, `ethernet`, `adc`, `ntc`, `embedded-systems`, `flowergraph`,
`telemetry`, `data-acquisition`.

---

Автор: [mamkincoderr](https://github.com/mamkincoderr)
