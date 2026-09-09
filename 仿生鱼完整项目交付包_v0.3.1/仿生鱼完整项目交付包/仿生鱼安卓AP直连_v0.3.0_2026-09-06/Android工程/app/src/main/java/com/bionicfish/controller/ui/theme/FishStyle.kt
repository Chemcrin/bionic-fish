package com.bionicfish.controller.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/** 恢复交付 v0.3.0 APK 中 DesignTokens.kt 的原值，保持现有页面间距与触控尺寸。 */
object FishSpacing {
    val xxs = 4.dp
    val xs = 8.dp
    val sm = 12.dp
    val md = 16.dp
    val lg = 24.dp
    val xl = 32.dp
    val minimumTouchTarget = 48.dp
    val contentMaxWidth = 920.dp
}

/** 与同一交付 APK 的浅色/深色状态颜色一致；不改变 Material 主题配色。 */
object FishStatusColors {
    val SuccessLight = Color(0xFF176B3A)
    val SuccessDark = Color(0xFF79D99D)
    val WarningLight = Color(0xFF7A4E00)
    val WarningDark = Color(0xFFFFCB73)
    val InfoLight = Color(0xFF005B70)
    val InfoDark = Color(0xFF67D3EC)
}
