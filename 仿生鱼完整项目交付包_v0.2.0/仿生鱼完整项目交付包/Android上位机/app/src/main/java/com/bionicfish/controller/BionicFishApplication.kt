package com.bionicfish.controller

import android.app.Application
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.preferencesDataStoreFile
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import com.bionicfish.controller.device.BionicFishRepository
import com.bionicfish.controller.device.DefaultBionicFishRepository
import com.bionicfish.controller.device.RepositoryConnectionPhase
import com.bionicfish.controller.settings.DataStoreSettingsStore
import com.bionicfish.controller.transport.AndroidBluetoothCapabilityProvider
import com.bionicfish.controller.transport.DefaultTransportFactory
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * 极简应用级依赖容器，并负责进程进入后台时的安全停止。
 *
 * 这里不持有 Activity，避免旋转屏幕时重建通信会话。Android 在强制结束进程时
 * 不保证执行生命周期回调，因此最终失联保护仍必须由 STM32 看门狗兜底。
 */
class BionicFishApplication : Application(), DefaultLifecycleObserver {
    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    lateinit var repository: BionicFishRepository
        private set

    override fun onCreate() {
        super<Application>.onCreate()
        val capabilityProvider = AndroidBluetoothCapabilityProvider(this)
        val dataStore = PreferenceDataStoreFactory.create(
            scope = applicationScope,
            produceFile = { preferencesDataStoreFile(SETTINGS_FILE_NAME) },
        )
        repository = DefaultBionicFishRepository(
            settingsStore = DataStoreSettingsStore(dataStore),
            transportFactory = DefaultTransportFactory(capabilityProvider),
            bluetoothCapabilityProvider = capabilityProvider,
            scope = applicationScope,
        )
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
    }

    override fun onStop(owner: LifecycleOwner) {
        val snapshot = repository.state.value
        if (
            snapshot.connection.phase == RepositoryConnectionPhase.CONNECTED &&
            snapshot.settings.safeStopOnBackground
        ) {
            applicationScope.launch {
                // 后台回调没有 UI 调用方替协程接住异常；仓库会把失败写入消息/日志，
                // 这里必须吞掉向上传播，避免 ACK 超时反而使应用进程崩溃。
                runCatching { repository.sendSafeStop("应用进入后台") }
            }
        }
    }

    private companion object {
        const val SETTINGS_FILE_NAME = "bionic_fish_settings.preferences_pb"
    }
}
