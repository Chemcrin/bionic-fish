package com.bionicfish.controller.settings

/** 来自用户提供的 AP直连_安卓端修改指引.md；本机尚未进行 ESP 实物联调。 */
object ApDirectDefaults {
    const val SSID = "BionicFish-AP"
    const val HOST = "192.168.4.1"
    const val PORT = 9000
    /** 仅作首次连接提示；应用不保存或自动提交 Wi-Fi 密码。 */
    const val PASSWORD = "12345678"
}
