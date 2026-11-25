# =============================================================================
# V307_FreeRTOS — CH32V307: FreeRTOS + lwIP на одном ядре QingKe V4F.
# Автор: Дмитрий (GitHub: mamkincoderr, https://github.com/mamkincoderr)
# Telegram: https://t.me/oDeXteRo
#
# Перенос H417_FreeRTOS на оценочную плату CH32V307V-R1-1v0. CMake + Ninja + RISC-V GCC 15 из MounRiver Studio 2.
# SDK — openwch/ch32v307 EVT (Core, Debug, Peripheral без правок; startup — три значения, помечены),
# FreeRTOS и lwIP 2.2.1 — из H417_FreeRTOS (порт RISC-V_V307 переделан под SysTick V307).
#   V307_FreeRTOS.elf / .hex / .bin  — флеш 0x00000, режим памяти 192 КБ код + 128 КБ ОЗУ
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(V307_FreeRTOS C ASM)

# Флаги V4F как в EVT WCH: аппаратный FPU одинарной точности + расширения WCH (xw).
set(CPU_FLAGS
    -march=rv32imafcxw
    -mabi=ilp32f
    -msmall-data-limit=8
    -msave-restore
    -fmessage-length=0
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    -fno-common
    -g
)

set(SDK_OPT  -Os)   # библиотеки: WCH, FreeRTOS, lwIP
set(USER_OPT -O0)   # свой код: оптимизация — атрибутом у каждой функции в .h

set(TP ${CMAKE_SOURCE_DIR}/ThirdParty)
set(LD ${CMAKE_SOURCE_DIR}/Ld/Link.ld)
file(GLOB PERIPH_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Peripheral/src/*.c)
file(GLOB USER_SRCS   CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/User/*.c)

# FreeRTOS: ядро + порт RISC-V_V307 + heap_4
set(RTOS_PORT ${TP}/FreeRTOS/portable/RISC-V_V307)
set(RTOS_SRCS
    ${TP}/FreeRTOS/event_groups.c
    ${TP}/FreeRTOS/list.c
    ${TP}/FreeRTOS/queue.c
    ${TP}/FreeRTOS/stream_buffer.c
    ${TP}/FreeRTOS/tasks.c
    ${TP}/FreeRTOS/timers.c
    ${TP}/FreeRTOS/portable/heap_4.c
    ${RTOS_PORT}/port.c
    ${RTOS_PORT}/portASM.S)
set(RTOS_INC
    ${TP}/FreeRTOS/include
    ${RTOS_PORT}
    ${RTOS_PORT}/chip_specific_extensions/RV32I_PFIC_no_extensions)

# lwIP: ядро IPv4, Ethernet, netconn, mDNS, NetBIOS, порт FreeRTOS
set(LWIP ${TP}/lwip/src)
file(GLOB LWIP_SRCS CONFIGURE_DEPENDS
    ${LWIP}/core/*.c
    ${LWIP}/core/ipv4/*.c
    ${LWIP}/api/*.c
    ${LWIP}/apps/mdns/*.c)
list(APPEND LWIP_SRCS
    ${LWIP}/netif/ethernet.c
    ${LWIP}/apps/netbiosns/netbiosns.c
    ${TP}/lwip/port_freertos/sys_arch.c)
set(LWIP_INC ${LWIP}/include ${TP}/lwip/port_freertos/include)

# Страница веб-сервера: web/index.html -> массив C при каждой правке страницы
set(WEB_PAGE_C ${CMAKE_BINARY_DIR}/web_page.c)
add_custom_command(
    OUTPUT ${WEB_PAGE_C}
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_SOURCE_DIR}/web/index.html -DOUT=${WEB_PAGE_C}
            -P ${CMAKE_SOURCE_DIR}/cmake/bin2c.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/web/index.html ${CMAKE_SOURCE_DIR}/cmake/bin2c.cmake
    COMMENT "web/index.html -> web_page.c"
    VERBATIM)

set(T ${PROJECT_NAME})

add_library(${T}_sdk OBJECT
    ${CMAKE_SOURCE_DIR}/Startup/startup_ch32v30x_D8C.S
    ${CMAKE_SOURCE_DIR}/Core/core_riscv.c
    ${CMAKE_SOURCE_DIR}/Debug/debug.c
    ${PERIPH_SRCS}
    ${RTOS_SRCS}
    ${LWIP_SRCS})
add_library(${T}_user OBJECT ${USER_SRCS} ${WEB_PAGE_C})

foreach(L ${T}_sdk ${T}_user)
    # printf (журнал в UART WCH-Link) — USART1, PA9
    target_compile_definitions(${L} PRIVATE DEBUG=DEBUG_UART1)
    target_include_directories(${L} PRIVATE
        ${CMAKE_SOURCE_DIR}/User
        ${CMAKE_SOURCE_DIR}/Core
        ${CMAKE_SOURCE_DIR}/Debug
        ${CMAKE_SOURCE_DIR}/Peripheral/inc
        ${RTOS_INC}
        ${LWIP_INC})
    target_compile_options(${L} PRIVATE ${CPU_FLAGS} $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)
endforeach()
target_compile_options(${T}_sdk  PRIVATE $<$<COMPILE_LANGUAGE:C>:${SDK_OPT}>)
target_compile_options(${T}_user PRIVATE ${USER_OPT} -Wall)
# Заголовки библиотек для своего кода — системные: -Wall не ругается на чужой код.
target_include_directories(${T}_user SYSTEM BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/Core
    ${CMAKE_SOURCE_DIR}/Debug
    ${CMAKE_SOURCE_DIR}/Peripheral/inc
    ${RTOS_INC}
    ${LWIP_INC})

add_executable(${T} $<TARGET_OBJECTS:${T}_sdk> $<TARGET_OBJECTS:${T}_user>)
target_link_options(${T} PRIVATE
    ${CPU_FLAGS}
    -T${LD}
    -nostartfiles
    -Wl,--gc-sections,--print-memory-usage
    -Wl,-Map,${T}.map
    --specs=nano.specs
    --specs=nosys.specs)
set_target_properties(${T} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${LD})

add_custom_command(TARGET ${T} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${T}> ${T}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${T}> ${T}.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${T}>
    BYPRODUCTS ${T}.hex ${T}.bin
    VERBATIM)
