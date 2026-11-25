# V307_FreeRTOS

面向 WCH `CH32V307V-R1-1v0` 开发板的 **12 通道电压表和 NTC 温度计**。
固件使用 FreeRTOS 11.1.0、lwIP 2.2.1、CMake、Ninja 和 WCH RISC-V GCC 15。

ADC1 与 ADC2 同时扫描 12 个模拟输入。固件使用内部 Vrefint 估算 VDDA，
计算 NTC 温度，并以 10 Hz 更新网页。网络地址由路由器的 DHCP 服务器分配；
本项目不运行 DHCP 服务器。

[Русский](README.md) · [English](README_EN.md) ·
[Windows 构建指南](docs/BUILDING.md) · [NTC 接线和引脚](docs/THERMISTORS.md)

![网页界面示意图。数值为演示数据，不是开发板的实时测量值。](docs/images/dashboard-preview.png)

## 硬件与软件

| 项目 | 配置 |
|---|---|
| 开发板 / MCU | WCH `CH32V307V-R1-1v0` / `CH32V307VCT6` |
| 内核 | QingKe V4F，RISC-V，112 MHz |
| 外部 ADC 输入 | 12 路：ADC1 `PA0–PA5`，ADC2 `PC0–PC5` |
| 内部 ADC 通道 | IN16 芯片温度；IN17 Vrefint |
| 网络 | 板载 10BASE-T PHY，DHCP 客户端、HTTP、mDNS、NetBIOS |

## 构建

需要 Windows、CMake 3.20 或更高版本、Ninja，以及 MounRiver Studio 2 提供的
WCH GCC 15 和 OpenOCD。MounRiver Studio 1.92 可选，用于 IDE 集成。

在仓库目录中运行：

```bat
build.bat          :: 编译，生成 obj\ 下的 ELF、HEX 和 BIN
build.bat clean    :: 删除构建目录
build.bat flash    :: 通过 WCH-Link 写入并校验 MCU
```

详细安装步骤和故障排查见[构建指南](docs/BUILDING.md)。`flash` 会写入连接的
微控制器。

## NTC 温度传感器

每路使用 `Murata NCP18WB333J03RB`（25 °C 时 33 kΩ，B25/85 = 4108 K）和一个
33 kΩ 固定电阻：NTC 接 +3.3 V，固定电阻接 GND，中间节点接一个 ADC 输入。
分压器必须与 VDDA 使用同一条 3.3 V 电源线。短采样时间下，建议在 ADC 输入
与 GND 之间、靠近 MCU 放置 10–100 nF 电容。

| 通道 | MCU 输入 | 板上引脚 |
|---|---|---|
| IN0–IN5 | PA0–PA5 / ADC1 | J6.1–J6.6 |
| IN10–IN15 | PC0–PC5 / ADC2 | J3.31、33、35、37、39、40 |

输入电压必须保持在 0…VDDA 范围，不能接 5 V。当前固件把 12 路外部输入都当作
NTC 分压器处理。详细电路和限制见[传感器指南](docs/THERMISTORS.md)。

## 搜索标签

`#CH32V307` `#CH32V307VCT6` `#WCH` `#QingKeV4F` `#RISC-V` `#FreeRTOS`
`#lwIP` `#Ethernet` `#ADC` `#NTC` `#MounRiver`
