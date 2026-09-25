package com.bionicfish.controller.transport

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

fun interface NetworkScanner {
    suspend fun scan(kind: TransportKind, request: ScanRequest): List<TransportCandidate>
}

/**
 * 可配置 UDP 服务发现。没有 ESP 端发现协议资料时不会发送猜测报文，只返回已保存端点。
 */
class UdpDiscoveryScanner(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : NetworkScanner {
    private val activeSocket = AtomicReference<DatagramSocket?>()

    override suspend fun scan(kind: TransportKind, request: ScanRequest): List<TransportCandidate> =
        withContext(ioDispatcher) {
            require(request.timeoutMillis in 100L..60_000L) { "扫描超时须为 100..60000 ms" }
            val candidates = linkedMapOf<String, TransportCandidate>()
            request.configuredEndpoint
                ?.takeIf { it.address.isNotBlank() && it.port in 1..65535 }
                ?.let {
                    candidates["${it.address}:${it.port}"] = TransportCandidate(
                        name = request.configuredName,
                        endpoint = it,
                        kind = kind,
                        discoverySource = DiscoverySource.SAVED,
                    )
                }

            if (
                request.discoveryPayload.isBlank() || request.discoveryAddress.isBlank() ||
                request.discoveryPort !in 1..65535
            ) {
                return@withContext candidates.values.toList()
            }

            val socket = DatagramSocket().apply {
                broadcast = true
                soTimeout = 150
            }
            activeSocket.set(socket)
            try {
                val bytes = request.discoveryPayload.toByteArray(StandardCharsets.US_ASCII)
                socket.send(
                    DatagramPacket(
                        bytes,
                        bytes.size,
                        InetAddress.getByName(request.discoveryAddress),
                        request.discoveryPort,
                    ),
                )
                val deadline = System.nanoTime() + request.timeoutMillis * 1_000_000L
                val response = ByteArray(512)
                while (System.nanoTime() < deadline) {
                    try {
                        val packet = DatagramPacket(response, response.size)
                        socket.receive(packet)
                        val name = String(packet.data, packet.offset, packet.length, StandardCharsets.UTF_8)
                            .trim()
                            .take(64)
                            .ifBlank { "ESP 服务" }
                        val endpoint = TransportEndpoint(packet.address.hostAddress.orEmpty(), packet.port)
                        candidates["${endpoint.address}:${endpoint.port}"] = TransportCandidate(
                            name = name,
                            endpoint = endpoint,
                            kind = kind,
                            // 普通 UDP 响应无法提供可信 RSSI，保持 null。
                            signalDbm = null,
                            discoverySource = DiscoverySource.UDP_RESPONSE,
                        )
                    } catch (_: SocketTimeoutException) {
                        // 小超时用于响应取消并检查总扫描期限。
                    }
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } finally {
                activeSocket.compareAndSet(socket, null)
                socket.close()
            }
            candidates.values.toList()
        }

    fun cancel() {
        activeSocket.getAndSet(null)?.close()
    }
}
