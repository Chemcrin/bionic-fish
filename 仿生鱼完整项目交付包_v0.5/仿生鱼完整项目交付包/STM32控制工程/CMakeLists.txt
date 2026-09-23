cmake_minimum_required(VERSION 3.20)
project(bionic_fish C ASM)

# Both entry scripts delegate to this file; this is the only firmware source list.
option(BIONIC_FISH_BUILD_HOST_TESTS "Build portable protocol/control checks" ON)
option(BIONIC_FISH_BUILD_FIRMWARE "Build STM32 firmware with an installed STM32CubeF1 package" OFF)
option(BIONIC_FISH_WERROR "Treat compiler warnings (including vendor HAL) as errors" OFF)
set(BIONIC_FISH_HSE_VALUE_HZ "8000000" CACHE STRING
    "HSE frequency in Hz; must exactly match CFG_HSE_VALUE_HZ in app/Inc/app_config.h")

if(BIONIC_FISH_BUILD_HOST_TESTS AND NOT BIONIC_FISH_BUILD_FIRMWARE)
    enable_testing()
    function(bionic_host_test target)
        target_compile_features(${target} PRIVATE c_std_11)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endfunction()
    add_executable(protocol_host_test
        tests/host/Src/protocol_host_test.c
        protocol/Src/ring_buffer.c
        protocol/Src/ascii_protocol.c
        control/Src/control_arbiter.c)
    target_include_directories(protocol_host_test PRIVATE
        app/Inc
        protocol/Inc
        control/Inc)
    bionic_host_test(protocol_host_test)

    add_executable(esp_link_host_test
        tests/esp_link/esp_link_host_test.c
        bsp/Src/esp_link.c
        bsp/Src/bsp_uart.c
        protocol/Src/ring_buffer.c
        protocol/Src/ascii_protocol.c)
    target_include_directories(esp_link_host_test PRIVATE tests/esp_link/stubs bsp/Inc app/Inc protocol/Inc)
    bionic_host_test(esp_link_host_test)

    foreach(target motor_host_test motor_disabled_host_test)
        add_executable(${target}
            tests/host/Src/motor_host_test.c
            control/Src/motor_control.c
            control/Src/control_arbiter.c)
        target_include_directories(${target} PRIVATE app/Inc control/Inc protocol/Inc bsp/Inc)
        bionic_host_test(${target})
    endforeach()
    target_compile_definitions(motor_disabled_host_test PRIVATE CFG_MOTOR_A_ENABLED=0)

    add_executable(bsp_pwm_host_test tests/host/Src/bsp_pwm_host_test.c bsp/Src/bsp_board.c)
    target_include_directories(bsp_pwm_host_test PRIVATE tests/host/pwm_stubs Core/Inc bsp/Inc app/Inc)
    bionic_host_test(bsp_pwm_host_test)

    foreach(target sensor_oled_host_test sensor_oled_default_host_test sensor_oled_unfiltered_host_test)
        add_executable(${target}
            tests/host/Src/sensor_oled_host_test.c
            drivers/Src/jy61p.c
            drivers/Src/ssd1306.c)
        target_include_directories(${target} PRIVATE tests/host/driver_stubs app/Inc drivers/Inc)
        bionic_host_test(${target})
    endforeach()
    target_compile_definitions(sensor_oled_host_test PRIVATE CFG_JY61P_MAX_DELTA_DDEG=300)
    target_compile_definitions(sensor_oled_unfiltered_host_test PRIVATE CFG_JY61P_MAX_DELTA_DDEG=0)

    foreach(target app_host_test app_disabled_host_test)
        add_executable(${target}
            tests/host/Src/app_host_test.c
            protocol/Src/ascii_protocol.c
            protocol/Src/http_ui.c
            control/Src/control_arbiter.c
            control/Src/motor_control.c
            control/Src/servo_control.c
            drivers/Src/jy61p.c
            drivers/Src/ssd1306.c
            ui/Src/oled_ui.c)
        target_include_directories(${target} PRIVATE
            tests/host/app_stubs Core/Inc app/Inc bsp/Inc control/Inc drivers/Inc protocol/Inc ui/Inc)
        bionic_host_test(${target})
    endforeach()
    target_compile_definitions(app_disabled_host_test PRIVATE CFG_MOTOR_A_ENABLED=0)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        function(configuration_check name should_fail)
            add_test(NAME ${name} COMMAND "${CMAKE_C_COMPILER}" -std=c11 -fsyntax-only
                "-I${CMAKE_CURRENT_SOURCE_DIR}/app/Inc"
                "-I${CMAKE_CURRENT_SOURCE_DIR}/control/Inc"
                "-I${CMAKE_CURRENT_SOURCE_DIR}/protocol/Inc"
                "-I${CMAKE_CURRENT_SOURCE_DIR}/bsp/Inc"
                ${ARGN} "${CMAKE_CURRENT_SOURCE_DIR}/tests/host/Src/motor_host_test.c")
            set_tests_properties(${name} PROPERTIES WILL_FAIL ${should_fail})
        endfunction()
        # 2026-09-12：步进换相宏已删除，改为覆盖两路直流电机的占空比/使能门禁。
        configuration_check(config_default_valid FALSE)
        configuration_check(config_motor_a_enabled_rejected TRUE -DCFG_MOTOR_A_ENABLED=2)
        configuration_check(config_motor_a_zero_duty_rejected TRUE -DCFG_MOTOR_A_DUTY_PERCENT=0)
        configuration_check(config_motor_a_high_duty_rejected TRUE -DCFG_MOTOR_A_DUTY_PERCENT=101)
        configuration_check(config_motor_b_enabled_rejected TRUE -DCFG_MOTOR_B_ENABLED=5)
        configuration_check(config_motor_b_zero_duty_rejected TRUE -DCFG_MOTOR_B_DUTY_PERCENT=0)
        configuration_check(config_vacuous_imu_rejected TRUE -DCFG_JY61P_MAX_DELTA_DDEG=1800)
    endif()
endif()

if(BIONIC_FISH_BUILD_FIRMWARE)
    if(NOT DEFINED STM32CUBE_F1_PATH)
        message(FATAL_ERROR "Set STM32CUBE_F1_PATH to the STM32CubeF1 firmware package root.")
    endif()
    file(TO_CMAKE_PATH "${STM32CUBE_F1_PATH}" STM32CUBE_F1_PATH)
    if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Firmware requires cmake/arm-none-eabi-gcc.cmake and Arm GNU Toolchain.")
    endif()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
    endif()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_cube_f1.cmake")

    set(CUBE_HAL_DIR "${STM32CUBE_F1_PATH}/Drivers/STM32F1xx_HAL_Driver")
    set(CUBE_CMSIS_DIR "${STM32CUBE_F1_PATH}/Drivers/CMSIS")
    set(CUBE_DEVICE_DIR "${CUBE_CMSIS_DIR}/Device/ST/STM32F1xx")
    set(CUBE_STARTUP "${CUBE_DEVICE_DIR}/Source/Templates/gcc/startup_stm32f103xb.s")
    if(NOT EXISTS "${CUBE_STARTUP}")
        message(FATAL_ERROR "STM32CubeF1 package is incomplete or does not contain ${CUBE_STARTUP}")
    endif()

    add_executable(bionic_fish.elf
        Core/Src/main.c
        Core/Src/assert.c
        Core/Src/gpio.c
        Core/Src/tim.c
        Core/Src/usart.c
        Core/Src/stm32f1xx_it.c
        Core/Src/stm32f1xx_hal_msp.c
        app/Src/app.c
        bsp/Src/bsp_board.c
        bsp/Src/bsp_time.c
        bsp/Src/bsp_uart.c
        bsp/Src/esp_link.c
        control/Src/control_arbiter.c
        control/Src/motor_control.c
        control/Src/servo_control.c
        drivers/Src/soft_i2c.c
        drivers/Src/jy61p.c
        drivers/Src/ssd1306.c
        protocol/Src/ring_buffer.c
        protocol/Src/ascii_protocol.c
        protocol/Src/http_ui.c
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
        -Wl,--gc-sections "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/bionic_fish.map" -Wl,--print-memory-usage
        --specs=nano.specs --specs=nosys.specs)
    set_property(TARGET bionic_fish.elf APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/STM32F103C8Tx_FLASH.ld")
    add_custom_command(TARGET bionic_fish.elf POST_BUILD
        # Relative filenames also work with Windows binutils in a Chinese path.
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE_NAME:bionic_fish.elf> bionic_fish.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE_NAME:bionic_fish.elf>
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM)
endif()
