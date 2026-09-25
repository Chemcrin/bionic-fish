package com.bionicfish.controller.ui

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Gamepad
import androidx.compose.material.icons.filled.MonitorHeart
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.bionicfish.controller.ui.components.AppBanner
import com.bionicfish.controller.ui.components.ConnectionBadge
import com.bionicfish.controller.ui.model.AppDestination
import com.bionicfish.controller.ui.model.AppUiState
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.UiActions
import com.bionicfish.controller.ui.screens.ControlScreen
import com.bionicfish.controller.ui.screens.ControlSafetyStopBar
import com.bionicfish.controller.ui.screens.DevicesScreen
import com.bionicfish.controller.ui.screens.SettingsScreen
import com.bionicfish.controller.ui.screens.TelemetryScreen
import com.bionicfish.controller.ui.theme.BionicFishTheme
import com.bionicfish.controller.ui.theme.FishSpacing

/**
 * 单 Activity 入口 UI。运行时的 ViewModel 只需提供 [state] 与 [actions]，
 * Composable 不直接扫描设备、读写套接字或解析协议。
 */
@Composable
fun BionicFishApp(
    state: AppUiState,
    actions: UiActions,
    darkTheme: Boolean? = null,
) {
    BionicFishTheme(darkTheme = darkTheme ?: isSystemInDarkTheme()) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val navController = rememberNavController()
            val currentEntry by navController.currentBackStackEntryAsState()
            val currentRoute = currentEntry?.destination?.route ?: state.initialDestination.route
            val wideNavigation = maxWidth >= 600.dp
            val compactHeader = maxWidth < 360.dp || LocalDensity.current.fontScale >= 1.5f
            val safeStopOnExit by rememberUpdatedState(state.settings.sendSafeStopOnBackground)
            val emergencyStopAction by rememberUpdatedState(actions.onEmergencyStop)

            // 同时覆盖底部导航、系统返回和 Activity 销毁导致的控制页离开。
            DisposableEffect(currentRoute) {
                val leavingControl = currentRoute == AppDestination.CONTROL.route
                onDispose {
                    if (leavingControl && safeStopOnExit) emergencyStopAction()
                }
            }

            val navigateTo: (AppDestination) -> Unit = { destination ->
                navController.navigate(destination.route) {
                    popUpTo(navController.graph.findStartDestination().id) {
                        saveState = true
                    }
                    launchSingleTop = true
                    restoreState = true
                }
            }

            if (wideNavigation) {
                Row(Modifier.fillMaxSize()) {
                    AppNavigationRail(currentRoute = currentRoute, onNavigate = navigateTo)
                    AppScaffold(
                        state = state,
                        actions = actions,
                        navController = navController,
                        showBottomNavigation = false,
                        compactHeader = compactHeader,
                        currentRoute = currentRoute,
                        onNavigate = navigateTo,
                        modifier = Modifier.weight(1f),
                    )
                }
            } else {
                AppScaffold(
                    state = state,
                    actions = actions,
                    navController = navController,
                    showBottomNavigation = true,
                    compactHeader = compactHeader,
                    currentRoute = currentRoute,
                    onNavigate = navigateTo,
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }
    }
}

@Composable
@OptIn(ExperimentalMaterial3Api::class)
private fun AppScaffold(
    state: AppUiState,
    actions: UiActions,
    navController: NavHostController,
    showBottomNavigation: Boolean,
    compactHeader: Boolean,
    currentRoute: String,
    onNavigate: (AppDestination) -> Unit,
    modifier: Modifier,
) {
    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = if (compactHeader) "仿生鱼" else "仿生鱼控制台",
                        style = MaterialTheme.typography.titleLarge,
                    )
                },
                actions = {
                    if (compactHeader) {
                        Text(
                            text = compactConnectionLabel(state.connection.phase),
                            style = MaterialTheme.typography.labelLarge,
                            color = if (state.connection.phase == ConnectionPhase.LOST) {
                                MaterialTheme.colorScheme.error
                            } else {
                                MaterialTheme.colorScheme.onSurfaceVariant
                            },
                            modifier = Modifier.padding(end = FishSpacing.md),
                        )
                    } else {
                        ConnectionBadge(
                            phase = state.connection.phase,
                            modifier = Modifier.padding(end = FishSpacing.sm),
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
        bottomBar = {
            Column(Modifier.fillMaxWidth()) {
                // 安全停止由根 Scaffold 测量并固定在底部；它不会被长提示条、滚动、
                // 横屏低高度或 200% 字体挤出可视区域。
                if (currentRoute == AppDestination.CONTROL.route) {
                    Box(
                        modifier = Modifier.fillMaxWidth(),
                        contentAlignment = Alignment.Center,
                    ) {
                        ControlSafetyStopBar(
                            connected = state.connection.phase == ConnectionPhase.CONNECTED,
                            control = state.control,
                            reduceMotion = state.settings.reduceMotion,
                            onStop = actions.onEmergencyStop,
                            modifier = Modifier
                                .widthIn(max = FishSpacing.contentMaxWidth)
                                .fillMaxWidth()
                                .padding(horizontal = FishSpacing.md, vertical = FishSpacing.xs),
                        )
                    }
                }
                if (showBottomNavigation) {
                    AppNavigationBar(currentRoute = currentRoute, onNavigate = onNavigate)
                }
            }
        },
    ) { scaffoldPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(scaffoldPadding),
        ) {
            state.banner?.let { banner ->
                AppBanner(
                    banner = banner,
                    onDismiss = actions.onDismissBanner,
                    onAction = actions.onBannerAction,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = FishSpacing.md, vertical = FishSpacing.xs),
                )
            }
            Box(Modifier.weight(1f)) {
                AppNavHost(
                    navController = navController,
                    state = state,
                    actions = actions,
                )
            }
        }
    }
}

@Composable
private fun AppNavHost(
    navController: NavHostController,
    state: AppUiState,
    actions: UiActions,
) {
    NavHost(
        navController = navController,
        startDestination = state.initialDestination.route,
        modifier = Modifier.fillMaxSize(),
        // 控制应用不依赖页面动效传达状态，统一关闭默认导航过渡；系统/应用减少动效
        // 设置下都不会出现非必要位移或淡入淡出，状态仍由文字和图标表达。
        enterTransition = { EnterTransition.None },
        exitTransition = { ExitTransition.None },
        popEnterTransition = { EnterTransition.None },
        popExitTransition = { ExitTransition.None },
    ) {
        composable(AppDestination.DEVICES.route) {
            DevicesScreen(
                connection = state.connection,
                devices = state.devices,
                reduceMotion = state.settings.reduceMotion,
                actions = actions,
                contentPadding = PaddingValues(0.dp),
                settings = state.settings,
                onOpenControl = {
                    navController.navigate(AppDestination.CONTROL.route) {
                        launchSingleTop = true
                    }
                },
            )
        }
        composable(AppDestination.CONTROL.route) {
            ControlScreen(
                connection = state.connection,
                control = state.control,
                actions = actions,
                contentPadding = PaddingValues(0.dp),
            )
        }
        composable(AppDestination.TELEMETRY.route) {
            TelemetryScreen(
                telemetry = state.telemetry,
                actions = actions,
                contentPadding = PaddingValues(0.dp),
            )
        }
        composable(AppDestination.SETTINGS.route) {
            SettingsScreen(
                settings = state.settings,
                connectionPhase = state.connection.phase,
                bluetoothHardwareAvailable = state.connection.bluetoothHardwareAvailable,
                actions = actions,
                contentPadding = PaddingValues(0.dp),
            )
        }
    }
}

@Composable
private fun AppNavigationBar(
    currentRoute: String,
    onNavigate: (AppDestination) -> Unit,
) {
    NavigationBar(modifier = Modifier.testTag("bottom_navigation")) {
        NavigationItems.forEach { item ->
            NavigationBarItem(
                modifier = Modifier.testTag("nav_${item.destination.route}"),
                selected = currentRoute == item.destination.route,
                onClick = { onNavigate(item.destination) },
                icon = { Icon(item.icon, contentDescription = null) },
                label = { Text(item.label) },
            )
        }
    }
}

@Composable
private fun AppNavigationRail(
    currentRoute: String,
    onNavigate: (AppDestination) -> Unit,
) {
    NavigationRail(modifier = Modifier.testTag("navigation_rail")) {
        NavigationItems.forEach { item ->
            NavigationRailItem(
                modifier = Modifier.testTag("nav_${item.destination.route}"),
                selected = currentRoute == item.destination.route,
                onClick = { onNavigate(item.destination) },
                icon = { Icon(item.icon, contentDescription = null) },
                label = { Text(item.label) },
            )
        }
    }
}

private data class NavigationItem(
    val destination: AppDestination,
    val label: String,
    val icon: ImageVector,
)

private val NavigationItems = listOf(
    NavigationItem(AppDestination.DEVICES, "设备", Icons.Default.Devices),
    NavigationItem(AppDestination.CONTROL, "控制", Icons.Default.Gamepad),
    NavigationItem(AppDestination.TELEMETRY, "状态", Icons.Default.MonitorHeart),
    NavigationItem(AppDestination.SETTINGS, "设置", Icons.Default.Settings),
)

private fun compactConnectionLabel(phase: ConnectionPhase): String = when (phase) {
    ConnectionPhase.DISCONNECTED -> "未连接"
    ConnectionPhase.SCANNING -> "扫描中"
    ConnectionPhase.CONNECTING -> "连接中"
    ConnectionPhase.DISCONNECTING -> "断开中"
    ConnectionPhase.CONNECTED -> "已连接"
    ConnectionPhase.LOST -> "已失联"
    ConnectionPhase.RECONNECTING -> "重连中"
}
