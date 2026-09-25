# STM32 烧录脚本

脚本位置：`STM32控制工程/scripts/flash_stm32.ps1`

## 首次准备

1. 安装 ST 的 **STM32CubeProgrammer**（包含 `STM32_Programmer_CLI.exe`）和 ST-LINK USB 驱动。
2. 用 SWD 连接目标板：`SWDIO→PA13`、`SWCLK→PA14`、`GND→GND`，并给 MCU 提供 3.3 V 逻辑电源。
3. 执行 `python scripts/build.py firmware --bootstrap`，生成 `build-firmware\bionic_fish.bin` 与 `.elf`。完整依赖、跨平台命令和 CubeMX 独立目录生成/合并政策见 [BUILDING.md](BUILDING.md)。

## 一键烧录

在本目录（`STM32控制工程`）打开 PowerShell：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\flash_stm32.ps1
```

也可以显式指定固件和烧录器路径：

```powershell
.\scripts\flash_stm32.ps1 `
  -Firmware .\build-firmware\bionic_fish.bin `
  -Programmer 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
```

BIN 写入 `0x08000000`，ELF/HEX 使用文件中的加载地址；随后执行校验（`-v`），最后复位运行（`-rst`）。`-NoReset` 仅省略最后的复位运行，不会持续拉低 NRST。

默认使用 `mode=NORMAL reset=SWrst freq=4000`，适用于文档中的基本 SWD 接线。只有需要复位下连接且已将 ST-LINK NRST 接到目标 NRST 时，才使用 `-UnderReset`；它选择 `mode=UR reset=HWrst`。硬件复位对 NRST 的要求及 `freq` 参数见 [ST 官方 CLI 说明](https://www.st.com/resource/en/user_manual/um2237-stm32cubeprogrammer-software-description-stmicroelectronics.pdf)。

> 首次上电调试时请先断开步进电机电源，仅给 MCU 供电；工程默认允许前进，采用 10% PWM 固定低速；STOP 和失联会停止输出。首次烧录后先完成通信握手，再进行电机试转。
