# oled-ui-astra 上游参考与兼容性边界

参考仓库：<https://github.com/AstraThreshold/oled-ui-astra>（原题目中的
`dcfsswindy/oled-ui-astra` 已重定向）。本工程审查时参考提交为
`88716a62f84c41838458fd159841fe67e6a86f87`（简称 `88716a6`），上游许可证为
GPL-3.0。

**本工程没有复制或链接该上游 Launcher/HALDreamCore/U8g2 源码。** 原因是上游固定
使用 SPI2/PA2/PA3/PB13/PB15 和动态内存/阻塞动画，分别与本工程的 ESP USART2、
TB6612、无动态内存和分块软件 I2C 要求冲突。`ui/oled_ui.c` 和
`drivers/ssd1306.c` 是固定内存的兼容性重构：保留状态页面、卡片式信息层级和服务式
刷新思路，但不是“原样移植”或 API/视觉等价实现。

若后续希望研究上游，在不污染主工程构建的目录中固定版本获取：

```text
git clone https://github.com/AstraThreshold/oled-ui-astra.git
git -C oled-ui-astra checkout 88716a62f84c41838458fd159841fe67e6a86f87
```

若复制、修改或发布任何 GPL-3.0 上游源码，必须按 GPLv3 履行相应的版权、许可证和
完整对应源码义务；在完成资源、引脚和阻塞行为重构前，不应把它直接加入此 F103C8
固件。
