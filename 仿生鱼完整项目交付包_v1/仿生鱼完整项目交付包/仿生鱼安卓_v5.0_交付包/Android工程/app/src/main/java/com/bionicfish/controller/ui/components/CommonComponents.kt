package com.bionicfish.controller.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.bionicfish.controller.ui.model.BannerLevel
import com.bionicfish.controller.ui.model.BannerUiModel
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.theme.FishSpacing
import com.bionicfish.controller.ui.theme.FishStatusColors

@Composable
fun ScreenTitle(
    title: String,
    subtitle: String,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.fillMaxWidth()) {
        Text(
            text = title,
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.semantics { heading() },
        )
        Spacer(Modifier.height(FishSpacing.xxs))
        Text(
            text = subtitle,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
fun AppBanner(
    banner: BannerUiModel,
    onDismiss: () -> Unit,
    onAction: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val icon: ImageVector
    val containerColor: Color
    val contentColor: Color
    val levelName: String
    when (banner.level) {
        BannerLevel.INFO -> {
            icon = Icons.Default.Info
            containerColor = MaterialTheme.colorScheme.primaryContainer
            contentColor = MaterialTheme.colorScheme.onPrimaryContainer
            levelName = "提示"
        }
        BannerLevel.SUCCESS -> {
            icon = Icons.Default.CheckCircle
            containerColor = MaterialTheme.colorScheme.secondaryContainer
            contentColor = MaterialTheme.colorScheme.onSecondaryContainer
            levelName = "成功"
        }
        BannerLevel.WARNING -> {
            icon = Icons.Default.Warning
            containerColor = MaterialTheme.colorScheme.tertiaryContainer
            contentColor = MaterialTheme.colorScheme.onTertiaryContainer
            levelName = "警告"
        }
        BannerLevel.ERROR -> {
            icon = Icons.Default.Error
            containerColor = MaterialTheme.colorScheme.errorContainer
            contentColor = MaterialTheme.colorScheme.onErrorContainer
            levelName = "错误"
        }
    }

    Card(
        colors = CardDefaults.cardColors(
            containerColor = containerColor,
            contentColor = contentColor,
        ),
        modifier = modifier
            .fillMaxWidth()
            .heightIn(max = 240.dp)
            .semantics {
                stateDescription = levelName
                liveRegion = LiveRegionMode.Polite
            },
    ) {
        Column(
            Modifier
                .padding(FishSpacing.md)
                .verticalScroll(rememberScrollState()),
        ) {
            Row(verticalAlignment = Alignment.Top) {
                Icon(icon, contentDescription = null, modifier = Modifier.size(24.dp))
                Spacer(Modifier.width(FishSpacing.sm))
                Text(
                    text = banner.message,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
            Spacer(Modifier.height(FishSpacing.xs))
            Row(
                horizontalArrangement = Arrangement.End,
                modifier = Modifier.fillMaxWidth(),
            ) {
                OutlinedButton(onClick = onDismiss) { Text("知道了") }
                if (banner.actionLabel != null) {
                    Spacer(Modifier.width(FishSpacing.xs))
                    Button(onClick = onAction) { Text(banner.actionLabel) }
                }
            }
        }
    }
}

@Composable
fun ConnectionBadge(
    phase: ConnectionPhase,
    modifier: Modifier = Modifier,
) {
    val darkSurface = MaterialTheme.colorScheme.surface.luminance() < 0.5f
    val successColor = if (darkSurface) FishStatusColors.SuccessDark else FishStatusColors.SuccessLight
    val warningColor = if (darkSurface) FishStatusColors.WarningDark else FishStatusColors.WarningLight
    val infoColor = if (darkSurface) FishStatusColors.InfoDark else FishStatusColors.InfoLight
    val (label, icon, color) = when (phase) {
        ConnectionPhase.DISCONNECTED -> Triple("未连接", Icons.Default.Info, MaterialTheme.colorScheme.onSurfaceVariant)
        ConnectionPhase.SCANNING -> Triple("扫描中", Icons.Default.Info, infoColor)
        ConnectionPhase.CONNECTING -> Triple("连接中", Icons.Default.Info, infoColor)
        ConnectionPhase.DISCONNECTING -> Triple("安全断开中", Icons.Default.Warning, warningColor)
        ConnectionPhase.CONNECTED -> Triple("已连接", Icons.Default.CheckCircle, successColor)
        ConnectionPhase.LOST -> Triple("已失联", Icons.Default.Error, MaterialTheme.colorScheme.error)
        ConnectionPhase.RECONNECTING -> Triple("重连中", Icons.Default.Warning, warningColor)
    }
    StatusBadge(
        text = label,
        icon = icon,
        contentColor = color,
        modifier = modifier,
    )
}

@Composable
fun StatusBadge(
    text: String,
    icon: ImageVector,
    contentColor: Color,
    modifier: Modifier = Modifier,
) {
    Surface(
        shape = MaterialTheme.shapes.extraLarge,
        color = contentColor.copy(alpha = 0.12f),
        contentColor = contentColor,
        modifier = modifier.semantics { stateDescription = text },
    ) {
        Row(
            modifier = Modifier.padding(horizontal = FishSpacing.sm, vertical = FishSpacing.xs),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(FishSpacing.xs))
            Text(text, style = MaterialTheme.typography.labelLarge)
        }
    }
}

@Composable
fun SectionCard(
    title: String,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Card(modifier = modifier.fillMaxWidth()) {
        Column(Modifier.padding(FishSpacing.md)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.semantics { heading() },
            )
            Spacer(Modifier.height(FishSpacing.sm))
            content()
        }
    }
}

@Composable
fun MetricCard(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    supporting: String? = null,
    valid: Boolean = true,
) {
    Card(
        colors = CardDefaults.cardColors(
            containerColor = if (valid) {
                MaterialTheme.colorScheme.surfaceVariant
            } else {
                MaterialTheme.colorScheme.errorContainer
            },
        ),
        modifier = modifier,
    ) {
        Column(Modifier.padding(FishSpacing.md)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = if (valid) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onErrorContainer,
            )
            Spacer(Modifier.height(FishSpacing.xxs))
            Text(
                text = value,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.SemiBold,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            if (supporting != null) {
                Spacer(Modifier.height(FishSpacing.xxs))
                Text(text = supporting, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
fun ResponsivePair(
    first: @Composable (Modifier) -> Unit,
    second: @Composable (Modifier) -> Unit,
    modifier: Modifier = Modifier,
) {
    BoxWithConstraints(modifier.fillMaxWidth()) {
        // 大字体时即使是平板也改为纵排，避免 200% 字体挤压按钮和状态文案。
        if (maxWidth >= 560.dp && LocalDensity.current.fontScale < 1.5f) {
            Row(horizontalArrangement = Arrangement.spacedBy(FishSpacing.sm)) {
                first(Modifier.weight(1f))
                second(Modifier.weight(1f))
            }
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.sm)) {
                first(Modifier.fillMaxWidth())
                second(Modifier.fillMaxWidth())
            }
        }
    }
}

@Composable
fun EmptyState(
    title: String,
    message: String,
    actionLabel: String,
    onAction: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = FishSpacing.xl),
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(FishSpacing.xs))
            Text(
                text = message,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(Modifier.height(FishSpacing.md))
            Button(onClick = onAction, contentPadding = PaddingValues(horizontal = FishSpacing.lg, vertical = FishSpacing.sm)) {
                Text(actionLabel)
            }
        }
    }
}

@Composable
fun LoadingState(
    message: String,
    modifier: Modifier = Modifier,
    reduceMotion: Boolean = false,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(FishSpacing.md),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (reduceMotion) {
            Icon(Icons.Default.Info, contentDescription = null, modifier = Modifier.size(24.dp))
        } else {
            CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 3.dp)
        }
        Spacer(Modifier.width(FishSpacing.sm))
        Text(message, style = MaterialTheme.typography.bodyMedium)
    }
}

@Composable
fun ScrollableScreen(
    contentPadding: PaddingValues,
    modifier: Modifier = Modifier,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit,
) {
    Box(modifier.fillMaxWidth(), contentAlignment = Alignment.TopCenter) {
        Column(
            modifier = Modifier
                .widthIn(max = FishSpacing.contentMaxWidth)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(contentPadding)
                .padding(horizontal = FishSpacing.md, vertical = FishSpacing.sm),
            verticalArrangement = Arrangement.spacedBy(FishSpacing.md),
            content = content,
        )
    }
}
