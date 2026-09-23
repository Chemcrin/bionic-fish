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
    /** 优先停止目标中的一路，保留另一电机与舵机目标；不同于双路安全停止。 */
    suspend fun sendMotorStop(input: ControlInput)
    suspend fun sendSafeStop(reason: String = "安全停止")
    suspend fun exportLogs(): String
    suspend fun updateSettings(settings: AppSettings)
    fun dismissMessage(messageId: Long)
    fun clearLogs()
}
