# Сборка V307_FreeRTOS в Windows

Эталонный способ сборки — `build.bat` из корня репозитория. Он настраивает
CMake при первом запуске, запускает Ninja и создаёт ELF, Intel HEX и BIN. IDE
MounRiver Studio 1.92 может вызывать тот же скрипт через Build Target.

## 1. Установите инструменты

### Обязательно для сборки

- Windows 10/11.
- CMake **3.20 или новее**.
- Ninja.
- MounRiver Studio 2 с **WCH RISC-V GCC 15** и **OpenOCD**.
- PowerShell 5.1 или PowerShell 7.

### Для работы через IDE (не обязательно)

- MounRiver Studio 1.92 — открыть проект и запускать сборку из IDE.
- WCH-Link — нужен только для операций с подключённым чипом: `flash`, `reset`,
  `erase` и чтения `info`.

Установить CMake и Ninja можно из PowerShell:

```powershell
winget install --id Kitware.CMake --exact
winget install --id Ninja-build.Ninja --exact
```

Закройте и снова откройте PowerShell после установки. Проверьте доступность:

```powershell
cmake --version
ninja --version
```

Установите MounRiver Studio 2 с официальной страницы загрузок MounRiver. В ряде
сборок GCC 15 поставляется архивом `RISC-V Embedded GCC15.zip`, а не готовой
папкой. Распакуйте его в каталог `Toolchain`, не добавляя лишний уровень
вложенности. Скрипт должен найти файл вида:

```text
...
MounRiver_Studio2\resources\app\resources\win32\components\WCH\
Toolchain\RISC-V Embedded GCC15\bin\riscv32-wch-elf-gcc.exe
```

OpenOCD должен находиться в поставке MounRiver Studio 2 по пути вида:

```text
...\components\WCH\OpenOCD\OpenOCD\bin\openocd.exe
```

`build.bat` ищет эти каталоги на дисках C:, D:, E: и F:. Если MounRiver
установлен в другое место, задайте пути вручную; значения должны указывать на
каталоги `bin`:

```powershell
$env:WCH_TOOLCHAIN_DIR = 'D:\MounRiver\...\RISC-V Embedded GCC15\bin'
$env:WCH_OPENOCD_DIR   = 'D:\MounRiver\...\OpenOCD\OpenOCD\bin'
```

Эти переменные действуют в текущем окне PowerShell. Чтобы сохранить их для
следующих запусков, используйте параметры Windows «Переменные среды».

## 2. Соберите проект

Откройте PowerShell в корне проекта — там, где лежат `build.bat` и
`CMakeLists.txt` — и выполните:

```powershell
.\build.bat
```

Первый запуск создаёт Ninja-конфигурацию в `obj\`, затем компилирует и связывает
проект. Повторные запуски пересобирают только изменённые файлы.

Готовые файлы:

| Путь | Назначение |
|---|---|
| `obj\V307_FreeRTOS.elf` | ELF для отладки и анализа символов |
| `obj\V307_FreeRTOS.hex` | Intel HEX для программирования |
| `obj\V307_FreeRTOS.bin` | бинарный образ |
| `obj\V307_FreeRTOS.map` | карта памяти и символов линковщика |

`obj\` создаётся автоматически и исключён из Git.

## 3. Команды сборочного скрипта

| Команда | Действие |
|---|---|
| `build.bat` или `build.bat all` | Настроить CMake при необходимости и собрать проект |
| `build.bat clean` | Удалить каталог `obj\`; следующая сборка настроит его заново |
| `build.bat size` | Показать размеры секций собранного ELF |
| `build.bat info` | Прочитать CHIPID, UID, option bytes и режим памяти через WCH-Link |
| `build.bat reset` | Снять аппаратные точки останова, сбросить чип и запустить его |
| `build.bat flash` | Собрать, записать HEX через OpenOCD/WCH-Link, проверить и запустить |
| `build.bat erase` | Стереть флеш-память микроконтроллера |

`info`, `reset`, `flash` и `erase` требуют подключённую плату и WCH-Link. Команды
`flash` и `erase` меняют содержимое микроконтроллера; для одной лишь сборки
используйте `build.bat` без аргументов.

## 4. Сборка в MounRiver Studio 1.92

1. Откройте `V307_FreeRTOS.wvproj` в MounRiver Studio 1.92.
2. Убедитесь, что MounRiver Studio 2, CMake и Ninja установлены как описано выше.
3. В Project Explorer вызовите **Build** или цель `all`. Проект запускает
   `build.bat`, поэтому сообщения и файлы сборки те же, что и в PowerShell.
4. Ищите результат в `obj\V307_FreeRTOS.*`.

Сборка из командной строки — основной воспроизводимый способ. IDE debugger и
пошаговая отладка на этой ревизии платы отдельно не подтверждены.

## 5. Что делает сборка

- `CMakePresets.json` выбирает пресет `default` и генератор Ninja.
- `cmake/toolchain-wch-gcc15.cmake` находит `riscv32-wch-elf-gcc`.
- `tools/run.ps1` создаёт локальные junction `tools\gcc` и `tools\openocd` на
  установленный MounRiver Studio 2. Эти junction и `obj\` не коммитятся.
- `cmake/bin2c.cmake` преобразует `web/index.html` в C-массив, включённый в ELF.
- После линковки `objcopy` создаёт `.hex` и `.bin`, а GCC `size` печатает объём
  секций.

При ручном вызове CMake после `build.bat clean` используйте пресет проекта:

```powershell
cmake --preset default
cmake --build --preset default
```

Для обычной работы предпочтителен `build.bat`: он также готовит пути к
инструментам и сохраняет логику, используемую MounRiver Studio.

## 6. Если сборка не начинается

| Сообщение / симптом | Проверьте |
|---|---|
| `...gcc.exe не найден` | Распакован ли GCC 15; указывает ли `WCH_TOOLCHAIN_DIR` на `bin` |
| `openocd.exe не найден` | Установлен ли OpenOCD из MounRiver Studio 2; задан ли `WCH_OPENOCD_DIR` |
| `cmake` или `ninja` не найдены | Выполните `cmake --version`, `ninja --version`; откройте новое окно PowerShell |
| Ошибка после перемещения папки проекта | Выполните `build.bat clean`, затем `build.bat` |
| В MounRiver Studio сообщение о неудачной сборке | Сначала проверьте `build.bat` из PowerShell: IDE может показывать stderr OpenOCD как ошибку |
| Не удаётся подключить WCH-Link | Это относится к аппаратным командам, а не к компиляции; проверьте USB, питание и режим RISC-V |

## 7. Образ и параметры платы

Цель сборки — `CH32V307VCT6` на плате `CH32V307V-R1-1v0`, QingKe V4F с аппаратным
FPU. Основная частота — 112 МГц. Код собирается под режим памяти 192 КБ Flash +
128 КБ RAM; при первом запуске `MemConfig()` применяет option bytes и выполняет
сброс. Это поведение прошивки, а не шаг CMake.

Подробности сети и подключения аналоговых входов приведены в
[README](../README.md) и [руководстве NTC](THERMISTORS.md).
