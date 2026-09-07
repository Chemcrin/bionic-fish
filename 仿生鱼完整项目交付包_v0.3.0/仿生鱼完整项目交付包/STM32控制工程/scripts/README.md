# STM32 烧录脚本

脚本位置：`STM32控制工程/scripts/flash_stm32.ps1`

## 首次准备

1. 安装 ST 的 **STM32CubeProgrammer**（包含 `STM32_Programmer_CLI.exe`）和 ST-LINK USB 驱动。
2. 用 SWD 连接目标板：`SWDIO→PA13`、`SWCLK→PA14`、`GND→GND`，并给 MCU 提供 3.3 V 逻辑电源。
3. 先在 STM32CubeIDE/CubeMX 中构建，生成 `build-firmware\bionic_fish.bin`（或 `.elf/.hex`）。

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

脚本固定写入 `0x08000000`，执行校验（`-v`），最后复位运行（`-rst`）。若只想保持复位状态，可加 `-NoReset`。

> 首次上电调试时请先断开步进电机电源，仅给 MCU 供电；工程默认关闭步进输出安全门，确认线圈、限流和 STBY 后再接电机。
