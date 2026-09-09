package com.bionicfish.controller.settings

/** 当前 STM32 ESP-AT/TCP 固件的 AP 默认值；必须与 app_config.h 同步。 */
object ApDirectDefaults {
    const val SSID = "BionicFish-AP"
    const val HOST = "192.168.4.1"
    const val PORT = 9000
    /** 仅作首次连接提示；应用不保存或自动提交 Wi-Fi 密码。 */
    const val PASSWORD = "12345678"
}
