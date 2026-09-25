package com.bionicfish.controller.transport

import java.io.Closeable
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext

/** 只抽离 Socket 的创建/网络绑定；TCP 字节流和 V1 协议继续使用原有实现。 */
fun interface TcpSocketConnector {
    suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long): TcpSocketConnection
}

/** 一个连接独占自己的网络监听器。关闭、连接失败、取消、Wi-Fi 丢失均只回收一次。 */
class TcpSocketConnection(
    val socket: Socket,
    private val releaseNetwork: () -> Unit = {},
) : Closeable {
    private val closed = AtomicBoolean(false)
    private val failure = AtomicReference<String?>(null)
    val failureMessage: String? get() = failure.get()
    val isClosed: Boolean get() = closed.get()

    fun failAndClose(message: String) {
        failure.compareAndSet(null, message)
        close()
    }

    override fun close() {
        if (closed.compareAndSet(false, true)) {
            try {
                runCatching { socket.close() }
            } finally {
                runCatching { releaseNetwork() }
            }
        }
    }
}

class DefaultTcpSocketConnector(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : TcpSocketConnector {
    override suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long): TcpSocketConnection {
        check(!endpoint.wifiOnly) { AP_WIFI_REQUIRED_MESSAGE }
        return connectTcpSocket(endpoint, timeoutMillis, ioDispatcher) { TcpSocketConnection(Socket()) }
    }
}

internal const val AP_WIFI_REQUIRED_MESSAGE =
    "请先连接仿生鱼热点，再重试 AP 直连；可打开 Wi-Fi 设置检查连接。未找到能直达目标地址的 Wi-Fi 网络。"

/**
 * connect() 本身会阻塞，因此始终放在 IO 线程；取消时立即 close Socket 解除系统调用。
 * 外层 catch 还覆盖 withContext 返回调度时的取消窗口，防止已连通的 Socket 丢失所有者。
 */
internal suspend fun connectTcpSocket(
    endpoint: TransportEndpoint,
    timeoutMillis: Long,
    ioDispatcher: CoroutineDispatcher,
    createConnection: () -> TcpSocketConnection,
): TcpSocketConnection {
    endpoint.requireNetworkEndpoint()
    require(timeoutMillis in 100L..60_000L) { "连接超时须为 100..60000 ms" }
    var opened: TcpSocketConnection? = null
    try {
        return withContext(ioDispatcher) {
            currentCoroutineContext().ensureActive()
            val connection = createConnection()
            opened = connection
            suspendCancellableCoroutine { continuation ->
                continuation.invokeOnCancellation { connection.close() }
                if (!continuation.isActive) return@suspendCancellableCoroutine
                try {
                    connection.socket.tcpNoDelay = true
                    connection.socket.keepAlive = true
                    // AP 端点必须为 IPv4 字面量，避免主机名解析使用蜂窝默认 DNS。
                    val target = if (endpoint.wifiOnly) {
                        InetSocketAddress(requireApIpv4(endpoint.address), endpoint.port!!)
                    } else {
                        InetSocketAddress(endpoint.address, endpoint.port!!)
                    }
                    connection.socket.connect(target, timeoutMillis.toInt())
                    continuation.resume(connection) { _, value, _ -> value.close() }
                } catch (error: Exception) {
                    connection.close()
                    continuation.resumeWithException(error)
                }
            }
        }
    } catch (error: Throwable) {
        opened?.close()
        throw error
    }
}

/** 只解析字面量，不调用 DNS；AP 模式不接受主机名或 IPv6。 */
internal fun requireApIpv4(address: String): Inet4Address {
    val octets = address.split('.')
    require(octets.size == 4 && octets.all { part ->
        part.isNotEmpty() && part.all(Char::isDigit) &&
            part.length <= 3 && part.toIntOrNull() in 0..255
    }) { "AP 地址必须是 IPv4 地址，例如 192.168.4.1；请检查 AP 连接设置" }
    return InetAddress.getByAddress(octets.map { it.toInt().toByte() }.toByteArray()) as Inet4Address
}

/** 必须存在非默认、无网关的 IPv4 直连路由，不能仅凭默认 Wi-Fi 路由放行任意 AP 地址。 */
internal fun isDirectIpv4Route(
    target: Inet4Address,
    routeAddress: InetAddress,
    prefixLength: Int,
    hasGateway: Boolean,
): Boolean {
    if (routeAddress !is Inet4Address || hasGateway || prefixLength !in 1..32) return false
    val targetBytes = target.address
    val routeBytes = routeAddress.address
    for (index in 0..3) {
        val bits = (prefixLength - index * 8).coerceIn(0, 8)
        val mask = (0xff shl (8 - bits)) and 0xff
        if ((targetBytes[index].toInt() and mask) != (routeBytes[index].toInt() and mask)) return false
    }
    return true
}
