package com.bionicfish.controller.transport

import android.bluetooth.BluetoothAdapter
import android.content.Context
import android.content.pm.PackageManager
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow

data class BluetoothCapabilities(
    val classicHardwarePresent: Boolean,
    val bleHardwarePresent: Boolean,
)

fun interface BluetoothCapabilityProvider {
    fun detect(): BluetoothCapabilities
}

class AndroidBluetoothCapabilityProvider(
    private val context: Context,
) : BluetoothCapabilityProvider {
    @Suppress("DEPRECATION")
    override fun detect(): BluetoothCapabilities {
        val manager = context.packageManager
        val adapterPresent = BluetoothAdapter.getDefaultAdapter() != null
        return BluetoothCapabilities(
            classicHardwarePresent = adapterPresent &&
                manager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH),
            bleHardwarePresent = adapterPresent &&
                manager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE),
        )
    }
}

/**
 * ESP-01S 不具备蓝牙能力。本占位实现只让 UI 明确显示“未配置/不支持”，绝不扫描或伪装 ESP。
 */
class BluetoothPlaceholderTransport(
    override val kind: TransportKind,
    private val hardwarePresent: Boolean,
) : Transport {
    init {
        require(kind == TransportKind.BLUETOOTH_CLASSIC || kind == TransportKind.BLUETOOTH_LE)
    }

    private val reason = if (hardwarePresent) {
        "手机检测到蓝牙硬件，但本项目未配置外接蓝牙模块、地址和 UUID；ESP-01S 仅支持 Wi-Fi"
    } else {
        "本机未检测到对应蓝牙硬件"
    }
    private val mutableConnectionState = MutableStateFlow<TransportConnectionState>(
        TransportConnectionState.Unsupported(reason),
    )
    private val mutableIncomingBytes = MutableSharedFlow<ByteArray>()
    private val mutableFailures = MutableSharedFlow<TransportFailure>(extraBufferCapacity = 1)

    override val connectionState: StateFlow<TransportConnectionState> = mutableConnectionState.asStateFlow()
    override val incomingBytes: SharedFlow<ByteArray> = mutableIncomingBytes.asSharedFlow()
    override val failures: SharedFlow<TransportFailure> = mutableFailures.asSharedFlow()

    override suspend fun scan(request: ScanRequest): List<TransportCandidate> = emptyList()
    override fun cancelScan() = Unit
    override suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long): Unit = unsupported()
    override suspend fun disconnect(reason: String) = Unit
    override suspend fun send(bytes: ByteArray): Unit = unsupported()

    private fun unsupported(): Nothing {
        mutableConnectionState.value = TransportConnectionState.Unsupported(reason)
        mutableFailures.tryEmit(TransportFailure("bluetooth", reason, recoverable = false))
        throw UnsupportedOperationException(reason)
    }
}

class DefaultTransportFactory(
    private val bluetoothCapabilities: BluetoothCapabilityProvider,
    private val scanner: NetworkScanner = UdpDiscoveryScanner(),
) : TransportFactory {
    override fun create(kind: TransportKind): Transport = when (kind) {
        TransportKind.TCP -> TcpTransport(scanner)
        TransportKind.UDP -> UdpTransport(scanner)
        TransportKind.MOCK -> MockTransport()
        TransportKind.BLUETOOTH_CLASSIC -> BluetoothPlaceholderTransport(
            kind,
            bluetoothCapabilities.detect().classicHardwarePresent,
        )
        TransportKind.BLUETOOTH_LE -> BluetoothPlaceholderTransport(
            kind,
            bluetoothCapabilities.detect().bleHardwarePresent,
        )
    }
}
