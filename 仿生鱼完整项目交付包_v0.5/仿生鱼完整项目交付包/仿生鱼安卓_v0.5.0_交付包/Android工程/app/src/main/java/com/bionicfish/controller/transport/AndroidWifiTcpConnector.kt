package com.bionicfish.controller.transport

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import java.io.IOException
import java.net.Inet4Address
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers

/**
 * AP 模式只把本次 TCP Socket 绑定到已连接的 Wi-Fi，移动数据仍可用于手机其它业务。
 * 不读取 SSID，不假称验证了热点名称；原有握手仅验证 V1 兼容性，不证明设备身份。
 * 不请求 INTERNET/VALIDATED：ESP 热点没有外网是正常情况。
 */
class AndroidWifiTcpConnector(
    context: Context,
    private val defaultConnector: TcpSocketConnector = DefaultTcpSocketConnector(),
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : TcpSocketConnector {
    private val connectivity = context.applicationContext
        .getSystemService(ConnectivityManager::class.java)

    override suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long): TcpSocketConnection {
        if (!endpoint.wifiOnly) return defaultConnector.connect(endpoint, timeoutMillis)
        val target = requireApIpv4(endpoint.address)
        try {
            return connectTcpSocket(endpoint, timeoutMillis, ioDispatcher) {
                createWifiConnection(target)
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            if (error.message == AP_WIFI_REQUIRED_MESSAGE) throw error
            throw IOException(
                "AP 直连失败：${error.message ?: "TCP 连接失败"}。请在 Wi-Fi 设置确认仍连接仿生鱼热点，" +
                    "选择保持无互联网连接，并检查 ESP 供电和 TCP 端口。",
                error,
            )
        }
    }

    // 必须找到可能不是默认网络、且无互联网的已连接 Wi-Fi，并兼容 API 26。
    // 只读一次快照，立即注册回调并二次核查，后续由回调维护；这里不轮询 allNetworks。
    @Suppress("DEPRECATION")
    private fun createWifiConnection(target: Inet4Address): TcpSocketConnection {
        val manager = connectivity ?: throw IOException(AP_WIFI_REQUIRED_MESSAGE)
        val network = manager.allNetworks.firstOrNull { candidate ->
            isWifiOnly(manager.getNetworkCapabilities(candidate)) &&
                hasDirectRoute(manager.getLinkProperties(candidate), target)
        } ?: throw IOException(AP_WIFI_REQUIRED_MESSAGE)

        lateinit var connection: TcpSocketConnection
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onLost(lost: Network) {
                if (lost == network) connection.failAndClose(WIFI_LOST_MESSAGE)
            }

            override fun onCapabilitiesChanged(changed: Network, capabilities: NetworkCapabilities) {
                if (changed == network && !isWifiOnly(capabilities)) connection.failAndClose(WIFI_LOST_MESSAGE)
            }

            override fun onLinkPropertiesChanged(changed: Network, properties: LinkProperties) {
                if (changed == network && !hasDirectRoute(properties, target)) {
                    connection.failAndClose(WIFI_LOST_MESSAGE)
                }
            }
        }
        val registration = NetworkCallbackRegistration { manager.unregisterNetworkCallback(callback) }
        connection = TcpSocketConnection(Socket(), releaseNetwork = registration::close)
        try {
            val request = NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
                .build()
            manager.registerNetworkCallback(request, callback)
            registration.markRegistered()
            // 防止查询快照与注册回调之间 Wi-Fi 已丢失：注册后再次核查同一 Network。
            if (!isWifiOnly(manager.getNetworkCapabilities(network)) ||
                !hasDirectRoute(manager.getLinkProperties(network), target) || connection.isClosed
            ) {
                throw IOException(AP_WIFI_REQUIRED_MESSAGE)
            }
            network.bindSocket(connection.socket)
            return connection
        } catch (error: Throwable) {
            connection.close()
            throw error
        }
    }

    private fun isWifiOnly(capabilities: NetworkCapabilities?): Boolean = capabilities != null &&
        capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) &&
        !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN) &&
        !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)

    private fun hasDirectRoute(properties: LinkProperties?, target: Inet4Address): Boolean =
        properties?.routes?.any { route ->
            // hasGateway() 要求 API 29；gateway 自 API 21 可用。null、0.0.0.0、:: 均表示无网关。
            val hasGateway = route.gateway?.isAnyLocalAddress == false
            isDirectIpv4Route(target, route.destination.address, route.destination.prefixLength, hasGateway)
        } == true

    private companion object {
        const val WIFI_LOST_MESSAGE =
            "AP Wi-Fi 已断开或目标网段已改变；请打开 Wi-Fi 设置重新连接仿生鱼热点，并选择保持无互联网连接。"
    }
}

/** 回调可能在 register 返回前关闭连接；无论先关闭还是先注册都必须最终注销且只注销一次。 */
internal class NetworkCallbackRegistration(private val unregister: () -> Unit) {
    private val registered = AtomicBoolean(false)
    private val closed = AtomicBoolean(false)

    fun markRegistered() {
        registered.set(true)
        releaseIfReady()
    }

    fun close() {
        closed.set(true)
        releaseIfReady()
    }

    private fun releaseIfReady() {
        if (closed.get() && registered.compareAndSet(true, false)) runCatching { unregister() }
    }
}
