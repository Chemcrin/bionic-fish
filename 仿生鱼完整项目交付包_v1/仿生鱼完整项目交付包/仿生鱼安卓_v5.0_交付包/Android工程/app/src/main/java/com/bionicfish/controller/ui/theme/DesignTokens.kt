package com.bionicfish.controller.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/** 所有页面共用的 4dp 网格与最小触控尺寸。 */
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

object FishStatusColors {
    val SuccessLight = Color(0xFF176B3A)
    val SuccessDark = Color(0xFF79D99D)
    val WarningLight = Color(0xFF7A4E00)
    val WarningDark = Color(0xFFFFCB73)
    val InfoLight = Color(0xFF005B70)
    val InfoDark = Color(0xFF67D3EC)
}

