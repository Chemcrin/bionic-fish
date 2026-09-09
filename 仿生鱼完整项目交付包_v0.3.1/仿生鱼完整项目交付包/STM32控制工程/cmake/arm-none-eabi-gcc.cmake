# 使用示例见 README.md。此文件仅指定交叉编译器，不替代 CubeMX 外设配置。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Optional absolute directory; no global PATH changes are necessary.
set(ARM_TOOLCHAIN_BIN "" CACHE PATH "Directory containing arm-none-eabi-gcc")
find_program(ARM_GCC arm-none-eabi-gcc HINTS "${ARM_TOOLCHAIN_BIN}" REQUIRED)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy HINTS "${ARM_TOOLCHAIN_BIN}" REQUIRED)
find_program(ARM_SIZE arm-none-eabi-size HINTS "${ARM_TOOLCHAIN_BIN}" REQUIRED)
set(CMAKE_C_COMPILER "${ARM_GCC}")
set(CMAKE_ASM_COMPILER "${ARM_GCC}")
set(CMAKE_OBJCOPY "${ARM_OBJCOPY}")
set(CMAKE_SIZE "${ARM_SIZE}")
