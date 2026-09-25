# 历史实现，不参与当前交付

本目录保留此前的 ESP 自定义透明桥源码。当前交付使用 ESP-01S AT/TCP 驱动，不编译、不烧录这里的代码。

- `ESP01S_AP_UDP_Bridge/`：历史 UDP 透明桥，已知回包丢失 LF，不能与当前 Android V1 分帧器直接配套。
- `ESP01S_TCP_Transparent_Bridge/`：历史 TCP 裸串口桥，不提供当前 STM32 所需的 AT 命令和 `+IPD` 封装。

恢复任何历史方案均需要独立适配及重新验收，不能将它们与当前 AT 路径混用。
