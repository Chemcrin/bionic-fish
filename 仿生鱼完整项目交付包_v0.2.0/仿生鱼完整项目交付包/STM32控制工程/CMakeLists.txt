cmake_minimum_required(VERSION 3.20)
project(bionic_fish_host_checks C ASM)

# 此 CMake 目标故意只验证不依赖 STM32 HAL 的协议/仲裁逻辑。
# 完整固件请按 README 用 STM32CubeIDE + STM32CubeF1 HAL 构建。
option(BIONIC_FISH_BUILD_HOST_TESTS "Build portable protocol/control checks" ON)
option(BIONIC_FISH_BUILD_FIRMWARE "Build STM32 firmware with an installed STM32CubeF1 package" OFF)
option(BIONIC_FISH_WERROR "Treat all firmware warnings (including vendor HAL) as errors" OFF)
set(BIONIC_FISH_HSE_VALUE_HZ "8000000" CACHE STRING
    "HSE frequency in Hz; must exactly match CFG_HSE_VALUE_HZ in app/Inc/app_config.h")

if(BIONIC_FISH_BUILD_HOST_TESTS AND NOT BIONIC_FISH_BUILD_FIRMWARE)
    enable_testing()
    add_executable(protocol_host_test
        tests/host/Src/protocol_host_test.c
        protocol/Src/ring_buffer.c
        protocol/Src/ascii_protocol.c
        control/Src/control_arbiter.c)
    target_compile_features(protocol_host_test PRIVATE c_std_11)
    target_compile_options(protocol_host_test PRIVATE -Wall -Wextra -Werror)
    target_include_directories(protocol_host_test PRIVATE
        app/Inc
        protocol/Inc
        control/Inc)
    add_test(NAME protocol_host_test COMMAND protocol_host_test)
endif()

if(BIONIC_FISH_BUILD_FIRMWARE)
    if(NOT DEFINED STM32CUBE_F1_PATH)
        message(FATAL_ERROR "Set STM32CUBE_F1_PATH to the STM32CubeF1 firmware package root.")
    endif()

    set(CUBE_HAL_DIR "${STM32CUBE_F1_PATH}/Drivers/STM32F1xx_HAL_Driver")
    set(CUBE_CMSIS_DIR "${STM32CUBE_F1_PATH}/Drivers/CMSIS")
    set(CUBE_DEVICE_DIR "${CUBE_CMSIS_DIR}/Device/ST/STM32F1xx")
    set(CUBE_STARTUP "${CUBE_DEVICE_DIR}/Source/Templates/gcc/startup_stm32f103xb.s")
    if(NOT EXISTS "${CUBE_STARTUP}")
        message(FATAL_ERROR "STM32CubeF1 package is incomplete or does not contain ${CUBE_STARTUP}")
    endif()

    add_executable(bionic_fish.elf
        Core/Src/main.c
        Core/Src/gpio.c
        Core/Src/tim.c
        Core/Src/usart.c
        Core/Src/stm32f1xx_it.c
        Core/Src/stm32f1xx_hal_msp.c
        app/Src/app.c
        bsp/Src/bsp_board.c
        bsp/Src/bsp_time.c
        bsp/Src/bsp_uart.c
        control/Src/control_arbiter.c
        control/Src/motor_control.c
        control/Src/servo_control.c
        drivers/Src/soft_i2c.c
        drivers/Src/jy61p.c
        drivers/Src/ssd1306.c
        protocol/Src/ring_buffer.c
        protocol/Src/ascii_protocol.c
        ui/Src/oled_ui.c
        "${CUBE_DEVICE_DIR}/Source/Templates/system_stm32f1xx.c"
        "${CUBE_STARTUP}"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_cortex.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_dma.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_flash.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_flash_ex.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_gpio.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_gpio_ex.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_pwr.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_rcc.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_rcc_ex.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_tim.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_tim_ex.c"
        "${CUBE_HAL_DIR}/Src/stm32f1xx_hal_uart.c")
    target_compile_features(bionic_fish.elf PRIVATE c_std_11)
    target_compile_definitions(bionic_fish.elf PRIVATE
        USE_HAL_DRIVER STM32F103xB HSE_VALUE=${BIONIC_FISH_HSE_VALUE_HZ}U)
    target_include_directories(bionic_fish.elf PRIVATE
        Core/Inc app/Inc bsp/Inc control/Inc drivers/Inc protocol/Inc ui/Inc
        "${CUBE_HAL_DIR}/Inc" "${CUBE_HAL_DIR}/Inc/Legacy"
        "${CUBE_CMSIS_DIR}/Include" "${CUBE_DEVICE_DIR}/Include")
    target_compile_options(bionic_fish.elf PRIVATE
        -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections
        -Wall -Wextra)
    if(BIONIC_FISH_WERROR)
        target_compile_options(bionic_fish.elf PRIVATE -Werror)
    endif()
    target_link_options(bionic_fish.elf PRIVATE
        -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
        "-T${CMAKE_CURRENT_SOURCE_DIR}/cmake/STM32F103C8Tx_FLASH.ld"
        -Wl,--gc-sections -Wl,-Map="${CMAKE_CURRENT_BINARY_DIR}/bionic_fish.map"
        --specs=nano.specs --specs=nosys.specs)
    add_custom_command(TARGET bionic_fish.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:bionic_fish.elf>
                "${CMAKE_CURRENT_BINARY_DIR}/bionic_fish.bin"
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:bionic_fish.elf>
        VERBATIM)
endif()
