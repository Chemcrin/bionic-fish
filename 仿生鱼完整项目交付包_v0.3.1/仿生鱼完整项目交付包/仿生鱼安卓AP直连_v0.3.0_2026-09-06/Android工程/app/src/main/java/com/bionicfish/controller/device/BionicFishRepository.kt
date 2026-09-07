package com.bionicfish.controller.device

import com.bionicfish.controller.protocol.ControlInput
import com.bionicfish.controller.settings.AppSettings
import kotlinx.coroutines.flow.StateFlow

interface BionicFishRepository {
    val state: StateFlow<RepositoryState>

    suspend fun scan()
    fun cancelScan()
    suspend fun connect(deviceId: String)
    suspend fun connectApDirect()
    suspend fun disconnect(reason: String = "用户断开")
    suspend fun sendControl(input: ControlInput)
    suspend fun sendSafeStop(reason: String = "安全停止")
    suspend fun exportLogs(): String
    suspend fun updateSettings(settings: AppSettings)
    fun dismissMessage(messageId: Long)
    fun clearLogs()
}
