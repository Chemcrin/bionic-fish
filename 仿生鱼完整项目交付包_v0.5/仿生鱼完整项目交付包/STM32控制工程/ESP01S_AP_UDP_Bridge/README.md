# 已停用：自定义透明桥方案

本轮交付只支持 **Android → TCP → ESP-01S AT 固件 → USART2 → STM32**。

原说明中“刷自定义桥后 STM32 不需要改动”的说法错误：当前 STM32 的 `esp_link.c` 使用 AT 指令及 `+IPD`，与裸 UART 透明桥不兼容。不要按旧说明烧录本目录的历史代码。

两份历史 sketch 已移到 `../legacy/ESP01S_AP_UDP_Bridge/` 和 `../legacy/ESP01S_TCP_Transparent_Bridge/`，各自独立目录，避免 Arduino 合并两个 `setup/loop`。这些文件仅作为历史参考，不参与构建或本轮验收，也不代表已完成实板测试。

当前连接、构建及验收入口见上一级 `README.md`。AP 直连默认热点 `BionicFish-AP`，地址 `192.168.4.1`，TCP 端口 `9000`；ESP 固件版本及引脚/Flash 适配要求以该说明为准。
