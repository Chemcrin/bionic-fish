package com.bionicfish.controller.transport

import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

enum class TransportKind {
    TCP,
    UDP,
    MOCK,
    BLUETOOTH_CLASSIC,
    BLUETOOTH_LE,
}

data class TransportEndpoint(
    val address: String,
    val port: Int? = null,
    /** AP 直连只使用匹配目标子网的 Wi-Fi，不得回退到蜂窝网络。 */
    val wifiOnly: Boolean = false,
) {
    fun requireNetworkEndpoint(): TransportEndpoint {
        require(address.isNotBlank()) { "IP/主机名尚未配置" }
        require(port in 1..65535) { "端口必须在 1..65535" }
        return this
    }
}

data class TransportCandidate(
    val name: String,
    val endpoint: TransportEndpoint,
    val kind: TransportKind,
    val signalDbm: Int? = null,
    val foundAtEpochMillis: Long = System.currentTimeMillis(),
    val discoverySource: DiscoverySource,
)

enum class DiscoverySource { UDP_RESPONSE, SAVED, MOCK, AP_DIRECT }

data class ScanRequest(
    val timeoutMillis: Long,
    val discoveryAddress: String = "",
    val discoveryPort: Int = 0,
    /** 发现报文由用户或 ESP 固件资料提供；空字符串表示禁用主动 UDP 探测。 */
    val discoveryPayload: String = "",
    val configuredEndpoint: TransportEndpoint? = null,
    val configuredName: String = "已保存设备",
)

sealed interface TransportConnectionState {
    data object Disconnected : TransportConnectionState
    data class Connecting(val endpoint: TransportEndpoint) : TransportConnectionState
    data class Connected(val endpoint: TransportEndpoint) : TransportConnectionState
    data class Disconnecting(val reason: String) : TransportConnectionState
    data class Failed(val message: String, val recoverable: Boolean) : TransportConnectionState
    data class Unsupported(val reason: String) : TransportConnectionState
}

data class TransportFailure(
    val operation: String,
    val message: String,
    val recoverable: Boolean,
    val cause: Throwable? = null,
)

interface Transport {
    val kind: TransportKind
    val connectionState: StateFlow<TransportConnectionState>
    val incomingBytes: SharedFlow<ByteArray>
    val failures: SharedFlow<TransportFailure>

    suspend fun scan(request: ScanRequest): List<TransportCandidate>
    fun cancelScan()
    suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long)
    suspend fun disconnect(reason: String = "用户断开")
    suspend fun send(bytes: ByteArray)
}

fun interface TransportFactory {
    fun create(kind: TransportKind): Transport
}
