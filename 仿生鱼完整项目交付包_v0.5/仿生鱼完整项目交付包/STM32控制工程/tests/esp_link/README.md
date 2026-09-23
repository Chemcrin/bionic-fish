# ESP-AT 链路回归

此测试直接编译生产 `esp_link.c`、`bsp_uart.c`、环形缓冲和 ASCII 解析器。只替换 HAL 与 ESP 对端，不复制一份链路实现。假 HAL 每毫秒最多双向各传 11 字节，ESP 对端必须收齐 AT 命令或 CIPSEND 声明的载荷长度后才回应。测试不连接串口、网络或实板。

在 STM32 控制工程根目录执行（GCC / MinGW）：

```sh
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I tests/esp_link/stubs -I bsp/Inc -I app/Inc -I protocol/Inc \
  tests/esp_link/esp_link_host_test.c bsp/Src/esp_link.c bsp/Src/bsp_uart.c \
  protocol/Src/ring_buffer.c protocol/Src/ascii_protocol.c \
  -o esp_link_host_test
./esp_link_host_test
```

也可运行根工程配置的 CTest 链路目标。Release 构建必须保留测试 `assert`，不能定义 `NDEBUG` 后把检查编译掉。

覆盖：初始化响应归属、非 0 连接 ID、复位与关闭、NONOS/ESP-AT 命令差异、UART 参数核查、ACK 优先与旧 STA 合并、每秒 10 帧持续负载、单个 IPD 内 30 条命令、8 KB 连续 IPD、第二客户端隔离、异常/超长头与截断、UART 环满/ORE/接收重挂失败、清丢失标志时的 ISR 竞争、发送失败/超时、队列饱和、毫秒计数回绕、固定 TCP 空闲超时、在途发送中主动关闭，以及旧关闭响应与新连接交错。

## 当前硬件路径

ESP-01S 为 1 MB Flash 时，优先核对 **NONOS SDK v3.0.4 的 AT 1.7.4 Nano（512+512）**，或同系列 1.7.5 Nano；不要把普通 2 MB AT 包直接套到 1 MB 模块。实际 Flash、AT UART 引脚、115200 8N1 无流控仍须读取实物确认。代码通过 `AT+GMR` 选择 NONOS 1.7.4/1.7.5 的 `_CUR` 命令；已正确安装在相应硬件上的 ESP-AT 2.x 使用其当前命令。它不推断实际 Flash 容量，也不烧录或迁移 ESP 固件。[官方 v3.0.4 发布说明](https://github.com/espressif/ESP8266_NONOS_SDK/releases/tag/v3.0.4)、[官方 AT 镜像说明](https://github.com/espressif/ESP8266_NONOS_SDK/blob/v3.0.4/bin/at/README.md)

每条配置等待各自响应，创建 TCP server 并成功设置 `CIPSTO=3` 后才对外就绪。先探测 AT、软件复位并等待 `ready`，然后关闭回显、识别版本、核查当前 UART，再配置 `app_config.h` 的 AP 名称/密码/IP/端口、DHCP 默认池、普通多连接模式、精简 IPD 头、主动接收和单客户端上限。连续三次配置失败后停止重试，保留诊断并等待模块复位。NONOS 使用不写 Flash 的 `_CUR` 命令；2.x 先设 `SYSSTORE=0`。DHCP 参数顺序不同：NONOS 为 `CWDHCP_CUR=0,1`，2.x 为 `CWDHCP=1,2`。[官方命令对照](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/AT_Command_Set_Comparison.html)

## 恢复与容量边界

- UART 丢失、非法 IPD、断开及发送失败统一产生事件；应用消费事件后清协议/序号并停机。旧收发帧不会迁移到新客户端。链路恢复后须重新完成 Android 的 STOP/ACK/STA 握手。
- 服务端固定 `CIPSTO=3`，正常 500 ms 保活不会触发它。ESP8266 ESP-AT 2.2 官方明确服务端单向发送不重置此计时；NONOS 文档支持同一命令，但未明确陈述双向计时细节，因此代码不依赖该细节完成应用保护。已经建立的有效 CMD 会话超过 1000 ms 时，应用还调用 `EspLink_CloseClient()`，立即隔离旧字节并在当前发送事务安全终结后对原 ID 发 `CIPCLOSE`；初次 TCP 握手前不会触发这一应用计时。这样无效流量或单向 STA 不能让失效控制会话一直占住唯一槽位。[ESP8266 2.2 官方 CIPSTO 语义](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html#at-cipsto-query-set-the-local-tcp-server-timeout)
- CIPSEND 先发头，收到 `>` 才发精确载荷，收到 `SEND OK` 才发下一事务；不额外添加 CRLF、不使用固定冷却限制吞吐。ACK/ERR 用原有固定存储合并完整帧，STA 只保留最新一帧；容量耗尽时安全断链，不假装 ACK 已送达。
- IPD 长度与 ASCII 单帧长度分离，按流处理，帧头限制数字位数及整数溢出；500 ms 是 IPD 字节间空闲上限，不是整段持续时间。固定内存并非无限吞吐承诺；超过 UART/应用处理能力的流量仍会触发可见丢失和停机。
- **不能证明 ESP 已退出 CIPSEND 数据阶段时，禁止盲发 AT+RST 或 +++。** 700 ms 发送超时先使应用断链停机；随后最多等 5 秒获取迟到的提示/终结响应。迟到 `>` 可用等长空格加 LF 完成本次已声明的载荷，再在终结响应后复位；若无终结证据，`EspLink_ResetNeeded()` 锁存为真，`LastError()` 指示检查/复位 ESP 的电源或 EN，直到收到真实 `ready`。MCU 单独复位后连 AT 探测都未回应，也不假定 ESP 的旧数据阶段已结束。该板未提供 ESP 硬件复位控制脚，软件无法承诺这类故障自动恢复。[Espressif CIPSEND 与仅透传可用的 +++](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)

宿主通过不等于实板通过。实板仍需验证电源、交叉接线、实际 AT 镜像/引脚、AP DHCP、Android Wi-Fi 绑定、STOP 握手、持续控制及掉电/串口错误停机。
