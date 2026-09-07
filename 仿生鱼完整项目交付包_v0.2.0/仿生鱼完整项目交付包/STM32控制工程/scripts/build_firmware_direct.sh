#!/usr/bin/env bash
# Direct gcc build of bionic_fish.elf/.bin, mirroring CMakeLists.txt (firmware target).
# Uses the arm-none-eabi-gcc bundled with STM32CubeIDE (no cmake needed).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CUBE=/c/Users/CloudyRain/STM32Cube/Repository/STM32Cube_FW_F1_V1.8.7
HAL="$CUBE/Drivers/STM32F1xx_HAL_Driver"
CMSIS="$CUBE/Drivers/CMSIS"
DEV="$CMSIS/Device/ST/STM32F1xx"
GCCBIN=/d/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin
CC="$GCCBIN/arm-none-eabi-gcc"
OBJCOPY="$GCCBIN/arm-none-eabi-objcopy"
SIZE="$GCCBIN/arm-none-eabi-size"

BUILD="$ROOT/build-firmware"
OBJ="$BUILD/obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"

# The project's stripped stm32f1xx_hal_conf.h omits the standard assert_param macro
# that every HAL source calls. Inject a no-op definition (== default USE_FULL_ASSERT=off
# behaviour) via a forced include, without touching the delivered source tree.
cat > "$BUILD/assert_param.h" <<'EOF'
#ifndef ASSERT_PARAM_FIX_H
#define ASSERT_PARAM_FIX_H
#define assert_param(expr) ((void)0)
#endif
EOF

DEFS="-DUSE_HAL_DRIVER -DSTM32F103xB -DHSE_VALUE=8000000U -include $BUILD/assert_param.h"
INCS="-I$ROOT/Core/Inc -I$ROOT/app/Inc -I$ROOT/bsp/Inc -I$ROOT/control/Inc \
 -I$ROOT/drivers/Inc -I$ROOT/protocol/Inc -I$ROOT/ui/Inc \
 -I$HAL/Inc -I$HAL/Inc/Legacy -I$CMSIS/Include -I$DEV/Include"
CFLAGS="-mcpu=cortex-m3 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections \
 -Wall -Wextra -std=c11 -O2 $DEFS $INCS"

REPO_SRCS="Core/Src/main.c Core/Src/gpio.c Core/Src/tim.c Core/Src/usart.c \
 Core/Src/stm32f1xx_it.c Core/Src/stm32f1xx_hal_msp.c app/Src/app.c \
 bsp/Src/bsp_board.c bsp/Src/bsp_time.c bsp/Src/bsp_uart.c bsp/Src/esp_link.c \
 control/Src/control_arbiter.c control/Src/motor_control.c control/Src/servo_control.c \
 drivers/Src/soft_i2c.c drivers/Src/jy61p.c drivers/Src/ssd1306.c \
 protocol/Src/ring_buffer.c protocol/Src/ascii_protocol.c ui/Src/oled_ui.c"
CUBE_SRCS="$DEV/Source/Templates/system_stm32f1xx.c \
 $HAL/Src/stm32f1xx_hal.c $HAL/Src/stm32f1xx_hal_cortex.c $HAL/Src/stm32f1xx_hal_dma.c \
 $HAL/Src/stm32f1xx_hal_flash.c $HAL/Src/stm32f1xx_hal_flash_ex.c $HAL/Src/stm32f1xx_hal_gpio.c \
 $HAL/Src/stm32f1xx_hal_gpio_ex.c $HAL/Src/stm32f1xx_hal_pwr.c $HAL/Src/stm32f1xx_hal_rcc.c \
 $HAL/Src/stm32f1xx_hal_rcc_ex.c $HAL/Src/stm32f1xx_hal_tim.c $HAL/Src/stm32f1xx_hal_tim_ex.c \
 $HAL/Src/stm32f1xx_hal_uart.c"

echo "Compiling C sources..."
i=0
for s in $REPO_SRCS; do
  b="$(basename "$s")"; o="$OBJ/${i}_${b%.c}.o"
  "$CC" $CFLAGS -c "$ROOT/$s" -o "$o"
  i=$((i+1))
done
for s in $CUBE_SRCS; do
  b="$(basename "$s")"; o="$OBJ/c${i}_${b%.c}.o"
  "$CC" $CFLAGS -c "$s" -o "$o"
  i=$((i+1))
done

echo "Assembling startup..."
"$CC" $CFLAGS -x assembler-with-cpp -c "$DEV/Source/Templates/gcc/startup_stm32f103xb.s" -o "$OBJ/startup.o"
OBJS="$OBJ/startup.o"; for o in "$OBJ"/*.o; do [ "$o" != "$OBJ/startup.o" ] && OBJS="$OBJS $o"; done

echo "Linking..."
"$CC" -mcpu=cortex-m3 -mthumb -mfloat-abi=soft \
  -T"$ROOT/cmake/STM32F103C8Tx_FLASH.ld" -Wl,--gc-sections \
  -Wl,-Map="$BUILD/bionic_fish.map" --specs=nano.specs --specs=nosys.specs \
  $OBJS -o "$BUILD/bionic_fish.elf"

echo "objcopy -> bin"
"$OBJCOPY" -O binary "$BUILD/bionic_fish.elf" "$BUILD/bionic_fish.bin"

echo "Size:"
"$SIZE" "$BUILD/bionic_fish.elf"
ls -l "$BUILD/bionic_fish.bin"
echo "BUILD OK"
