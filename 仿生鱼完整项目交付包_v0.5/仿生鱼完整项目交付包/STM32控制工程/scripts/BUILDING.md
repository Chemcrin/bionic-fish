# 固件构建与 CubeMX 工作约定

固件只有一条构建路径：`CMakeLists.txt`。`build.py` 和保留的旧 `build_firmware_direct.sh` 都调用它；旧脚本不再维护另一份源列表，也不再注入 `assert_param.h`。

## 固定依赖

| 组件 | 基线 |
| --- | --- |
| STM32CubeF1 | v1.8.6，与 `bionic_fish.ioc` 相同；Git commit `42472d601caa457b02d849c7de5f9792629ee324` |
| Arm GNU Toolchain | 13.3.Rel1，目标 `arm-none-eabi` |
| CMake | 3.31.8；工程最低要求 3.20 |
| Ninja | 1.12.1 |
| Python | 3.9 或更高，仅构建包装脚本需要 |
| 主机测试 C 编译器 | GCC / Clang；本机验证使用 MinGW-w64 GCC 8.1.0 |

下载地址、Windows 工具归档 SHA-256、CubeF1 及两个必要子模块的 commit 均固定在 `dependencies.lock.json`。CMake/Arm 校验值来自官方发布的 SHA-256 文件；Ninja 校验值来自固定版本官方发布归档的实算。HAL/CMSIS 保留在用户缓存，不提交到业务仓库。CMake 会拒绝其他 CubeF1 版本的 `package.xml`。

## Windows 首次构建

在 `STM32控制工程` 目录执行，先确保 Python 和 Git 可用：

```powershell
python scripts/build.py firmware --bootstrap
python scripts/build.py host --bootstrap --host-cc C:/path/to/native-gcc.exe
```

`--bootstrap` 从官方源下载校验后的工具和固定版本 CubeF1，默认缓存为 `%LOCALAPPDATA%/bionic-fish-build`。首次下载需要网络及磁盘空间；已有缓存会复用。所有 PATH 设置仅传给本次子进程，不安装驱动、不修改用户全局环境、不自动烧录。第二次无需 `--bootstrap`。

宿主测试不要指定 `arm-none-eabi-gcc`；它不能生成可在电脑运行的测试程序。若本机 `gcc` 已正确配置，`--host-cc` 可以省略。Release/MinSizeRel 测试也保留断言。

## 已有工具、Linux 和 macOS

同一 Python 包装脚本支持所有平台；Windows 自动下载的二进制不用于其他平台。Linux/macOS 安装对应平台的 Arm GNU 13.3.Rel1、CMake 和 Ninja，传入目录，或将工具加入当前终端 PATH：

```sh
python3 scripts/build.py firmware --bootstrap --toolchain-bin /opt/arm-gnu-toolchain/bin
python3 scripts/build.py host --host-cc /usr/bin/gcc
```

在非 Windows 平台，`--bootstrap` 仅获取固定版本 CubeF1。也可以直接指定已安装的 v1.8.6：

```sh
python3 scripts/build.py firmware --cube-path /path/to/STM32Cube_FW_F1_V1.8.6 --toolchain-bin /path/to/arm/bin --cmake /path/to/cmake --ninja /path/to/ninja
```

不使用 Python 的等价入口仍是 CMake：

```sh
cmake -S . -B build-firmware -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake -DARM_TOOLCHAIN_BIN=/path/to/arm/bin -DSTM32CUBE_F1_PATH=/path/to/STM32Cube_FW_F1_V1.8.6 -DBIONIC_FISH_BUILD_FIRMWARE=ON -DBIONIC_FISH_BUILD_HOST_TESTS=OFF -DCMAKE_BUILD_TYPE=MinSizeRel -DBIONIC_FISH_WERROR=ON
cmake --build build-firmware --parallel
```

换编译器或生成器时使用新的 `--build-dir`，不要复用另一工具链的 CMake 缓存。

## 输出与验收

固件输出在忽略的 `build-firmware/`：`bionic_fish.elf`、`bionic_fish.bin`、`bionic_fish.map`。Python 包装脚本额外生成 `build-info.json`，记录编译器、CMake、CubeF1、源提交及是否有未提交修改、产物长度和 SHA-256。它不会把带未提交修改的产物误称为该 Git 提交的干净构建。

链接脚本限定 STM32F103C8 的 64 KiB Flash、20 KiB SRAM，并预留 0x200 堆及 0x800 栈；超区会使链接失败。链接日志打印内存使用，`arm-none-eabi-size` 打印段统计。通过链接只证明静态容量合规，不替代堆栈运行峰值或目标板波形验证。烧录操作按 `scripts/README.md` 单独执行。

Arm GNU 13.3.Rel1 的 `newlib-nano/nosys` 在库解析阶段会报告 `_close`、`_lseek`、`_read`、`_write` 未实现；基线干净构建已确认这些符号随后被链接垃圾回收，未保留在最终 ELF。串口收发由 BSP 实现。构建保留这四条链接诊断；`BIONIC_FISH_WERROR` 检查的是编译器告警。Flash 初始化函数指针表显式标为只读，生成的 ELF LOAD 段分别为 RX / RW；链接脚本本身也列为重链接依赖。

## CubeMX 再生成政策

`Core/` 是受维护的 HAL 风格适配层，**禁止在交付源码目录直接点击 Generate Code 并将结果视为可烧录版本**。CubeMX 的 KeepUserCode 只保护 USER CODE 区，无法保留自定义的完整时钟映射、MSP、安全 GPIO 初始化、HAL 配置和构建源列表。

1. 将 `bionic_fish.ioc` 复制到仓库之外的独立临时目录，使用 STM32CubeF1 v1.8.6 生成。
2. 查看生成文件与受维护 `Core/` 的差异，只合并本次需要的引脚/外设变更；更新 `.ioc` 与 `app_config.h` 的同一配置。
3. 保留 `main.c` 中已标记的 Includes、PD、2、3、4 和 Error_Handler_Debug 区域。必须存在 `BSP_Time_Init`、`App_Init`、循环中的 `App_Process`，以及 TIM4、UART RX/TX/Error 回调向 BSP 的转发。
4. 特别复查两桥启动 coast、舵机初始脉宽、TIM4 更新中断、USART2/3、SWD 和两条软件 I²C 的开漏配置；保留标准 `assert_param` 定义与 `Core/Src/assert.c`。
5. 完整执行 host 测试和固件交叉构建，再执行目标板空载验收。

业务接入 USER CODE 标记用于辅助合并，并不表示当前整个 `Core/` 已通过 CubeMX 无损再生成验收。官方行为参见 [STM32CubeMX UM1718](https://www.st.com/resource/en/user_manual/dm00104712-stm32cubemx-for-stm32-configuration-and-initialization-c-code-generation-stmicroelectronics.pdf)。
