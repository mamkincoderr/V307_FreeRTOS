# V307_FreeRTOS

**A 12-channel voltmeter and NTC thermometer over Ethernet** for the WCH
`CH32V307V-R1-1v0` evaluation board. The firmware uses FreeRTOS 11.1.0, lwIP
2.2.1, CMake, Ninja, and WCH RISC-V GCC 15.

ADC1 and ADC2 scan 12 analog inputs in parallel. The firmware corrects voltage
using the internal Vrefint reading, converts each external input to an NTC
temperature, and serves a local web dashboard at 10 Hz. Network settings come
from the router's DHCP server; this project does not run a DHCP server.

[Русская версия](README.md) · [中文](README_ZH.md) · [Detailed build guide](docs/BUILDING.md) ·
[Thermistor wiring and pinout](docs/THERMISTORS.md)

![V307 dashboard preview with illustrative values, not live board measurements](docs/images/dashboard-preview.png)

*The page is the project's actual frontend. Values in this screenshot are illustrative; the board was offline when it was captured.*

## Hardware and software

| Item | Configuration |
|---|---|
| Board / MCU | WCH `CH32V307V-R1-1v0` / `CH32V307VCT6` |
| Core | QingKe V4F, RISC-V, 112 MHz |
| Memory | 192 KB Flash + 128 KB RAM |
| External ADC | 12 inputs: ADC1 `PA0–PA5`, ADC2 `PC0–PC5` |
| Internal ADC | IN16: die temperature; IN17: Vrefint for VDDA estimation |
| Network | On-board 10BASE-T PHY, DHCP client, HTTP, mDNS, NetBIOS |
| Build tools | Windows, CMake ≥ 3.20, Ninja, WCH GCC 15 and OpenOCD from MounRiver Studio 2 |

## Build

```bat
build.bat          :: build ELF, HEX and BIN under obj\
build.bat clean    :: remove the build directory
build.bat flash    :: build, program through WCH-Link and verify
```

The build script locates GCC 15 and OpenOCD in a standard MounRiver Studio 2
installation. For a non-standard installation, set `WCH_TOOLCHAIN_DIR` to the
compiler's `bin` directory and `WCH_OPENOCD_DIR` to the OpenOCD `bin` directory.
See the [step-by-step Windows build guide](docs/BUILDING.md) for installation,
troubleshooting, and MounRiver Studio integration. `flash` writes to the
connected MCU.

## Thermistor wiring

Each input uses one `Murata NCP18WB333J03RB` NTC (33 kΩ at 25 °C, B25/85 = 4108 K)
and one 33 kΩ fixed resistor. Connect the NTC to +3.3 V, the fixed resistor to
GND, and the junction to one ADC input. Power the divider from the same 3.3 V rail
as VDDA; temperature is calculated from the ratio `Vinput / VDDA`.

![NTC divider connected to one ADC input](docs/images/ntc-divider.png)

The [thermistor guide](docs/THERMISTORS.md) lists all 12 connector pins, the
divider equation, ADC acquisition-time guidance, and input-voltage limits.

## What the firmware does

- Scans 12 channels with two ADCs and circular DMA.
- Applies a first-order filter with `τ ≈ 20 ms`; publishes a measurement window
  every 50 ms.
- Converts raw readings to volts using VDDA estimated from internal Vrefint.
- Converts each external channel to the temperature of the supported NTC.
- Estimates MCU die temperature from the factory calibration point on IN16.
- Serves a built-in web page and JSON endpoint at `/api/meas`.
- Uses lwIP for DHCP client, mDNS (`v307.local`), and NetBIOS (`v307`).

The current firmware interprets all 12 external inputs as thermistor dividers.
For a general analog signal, the voltage reading is still meaningful, but the
temperature shown for that channel is not.

## Measurement status and limits

Ethernet, DHCP client, and the web dashboard have been checked on the board. A
25 November 2026 measurement recorded up to 163,840 scan cycles/s (1.97 million
samples/s across 12 channels) with about 18.5% CPU time in the ADC interrupt.
Voltage accuracy against a meter and temperature accuracy with physical NTCs
still need to be checked on the assembled sensor dividers.

The ADC uses a 1.5-cycle acquisition time for maximum throughput. Keep the
source impedance low; for the thermistor divider, place a 10–100 nF capacitor
from the ADC node to GND, close to the MCU. Ethernet is clocked from a 112 MHz
system clock; check MDC frequency and link stability when moving to another
board revision.

## Licensing

Project application code is released under [0BSD](LICENSE). Third-party files
retain their own terms: FreeRTOS is MIT, lwIP is BSD-3-Clause, and WCH peripheral
library / EVT files are restricted to WCH microcontrollers. See [LICENSE](LICENSE)
and source headers for the complete file list.

## Search tags

`#CH32V307` `#CH32V307VCT6` `#CH32V307V-R1-1v0` `#WCH` `#QingKeV4F`
`#RISC-V` `#FreeRTOS` `#lwIP` `#Ethernet` `#ADC` `#NTC` `#MounRiver`

Author: [mamkincoderr](https://github.com/mamkincoderr)
