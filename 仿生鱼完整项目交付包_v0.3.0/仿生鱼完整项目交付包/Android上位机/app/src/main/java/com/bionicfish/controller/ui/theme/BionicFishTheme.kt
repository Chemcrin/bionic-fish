package com.bionicfish.controller.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

private val LightColors = lightColorScheme(
    primary = Color(0xFF005B70),
    onPrimary = Color.White,
    primaryContainer = Color(0xFFB4EBFA),
    onPrimaryContainer = Color(0xFF002028),
    secondary = Color(0xFF4B6269),
    onSecondary = Color.White,
    secondaryContainer = Color(0xFFCDE7EF),
    onSecondaryContainer = Color(0xFF061F25),
    tertiary = Color(0xFF5A5D8F),
    onTertiary = Color.White,
    tertiaryContainer = Color(0xFFE1E0FF),
    onTertiaryContainer = Color(0xFF161B4A),
    error = Color(0xFFB3261E),
    onError = Color.White,
    errorContainer = Color(0xFFF9DEDC),
    onErrorContainer = Color(0xFF410E0B),
    background = Color(0xFFF7FAFC),
    onBackground = Color(0xFF191C1D),
    surface = Color(0xFFF7FAFC),
    onSurface = Color(0xFF191C1D),
    surfaceVariant = Color(0xFFDBE4E7),
    onSurfaceVariant = Color(0xFF3F484B),
    surfaceTint = Color(0xFF005B70),
    surfaceDim = Color(0xFFD7DBDD),
    surfaceBright = Color(0xFFF7FAFC),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFF1F5F7),
    surfaceContainer = Color(0xFFEBEFF1),
    surfaceContainerHigh = Color(0xFFE5E9EB),
    surfaceContainerHighest = Color(0xFFDFE3E5),
    outline = Color(0xFF6F797C),
    outlineVariant = Color(0xFFBFC8CB),
)

private val DarkColors = darkColorScheme(
    primary = Color(0xFF67D3EC),
    onPrimary = Color(0xFF003640),
    primaryContainer = Color(0xFF004E5F),
    onPrimaryContainer = Color(0xFFB4EBFA),
    secondary = Color(0xFFB1CBD3),
    onSecondary = Color(0xFF1C343B),
    secondaryContainer = Color(0xFF334A51),
    onSecondaryContainer = Color(0xFFCDE7EF),
    tertiary = Color(0xFFC2C3FF),
    onTertiary = Color(0xFF2B2E5E),
    tertiaryContainer = Color(0xFF414477),
    onTertiaryContainer = Color(0xFFE1E0FF),
    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF101415),
    onBackground = Color(0xFFE1E3E3),
    surface = Color(0xFF101415),
    onSurface = Color(0xFFE1E3E3),
    surfaceVariant = Color(0xFF3F484B),
    onSurfaceVariant = Color(0xFFBFC8CB),
    surfaceTint = Color(0xFF67D3EC),
    surfaceDim = Color(0xFF101415),
    surfaceBright = Color(0xFF363A3B),
    surfaceContainerLowest = Color(0xFF0B0F10),
    surfaceContainerLow = Color(0xFF191C1D),
    surfaceContainer = Color(0xFF1D2021),
    surfaceContainerHigh = Color(0xFF272A2B),
    surfaceContainerHighest = Color(0xFF323536),
    outline = Color(0xFF899296),
    outlineVariant = Color(0xFF3F484B),
)

private val AppShapes = Shapes(
    extraSmall = androidx.compose.foundation.shape.RoundedCornerShape(4.dp),
    small = androidx.compose.foundation.shape.RoundedCornerShape(8.dp),
    medium = androidx.compose.foundation.shape.RoundedCornerShape(12.dp),
    large = androidx.compose.foundation.shape.RoundedCornerShape(16.dp),
    extraLarge = androidx.compose.foundation.shape.RoundedCornerShape(24.dp),
)

@Composable
fun BionicFishTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) DarkColors else LightColors,
        typography = Typography(),
        shapes = AppShapes,
        content = content,
    )
}
