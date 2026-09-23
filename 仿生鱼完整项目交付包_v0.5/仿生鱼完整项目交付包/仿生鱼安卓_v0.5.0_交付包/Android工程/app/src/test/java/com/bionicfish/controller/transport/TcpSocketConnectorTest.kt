package com.bionicfish.controller.transport

import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketAddress
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class TcpSocketConnectorTest {
    private val endpoint = TransportEndpoint("192.168.4.1", 9000, wifiOnly = true)

    @Test
    fun `AP address parsing rejects DNS names and malformed addresses without resolving`() {
        assertEquals("192.168.4.1", requireApIpv4(endpoint.address).hostAddress)
        listOf("fish.local", "192.168.4.256", "192.168.4", "::1", "192.168..1", "192.168.4.-1").forEach { bad ->
            try {
                requireApIpv4(bad)
                fail("accepted $bad")
            } catch (_: IllegalArgumentException) {
                // 不能让 AP 主机名经默认蜂窝网络的 DNS 解析。
            }
        }
    }

    @Test
    fun `AP matches only its direct IPv4 subnet and never a default or gateway route`() {
        val target = requireApIpv4("192.168.4.1")
        val subnet = requireApIpv4("192.168.4.0")
        assertTrue(isDirectIpv4Route(target, subnet, 24, hasGateway = false))
        assertFalse(isDirectIpv4Route(target, requireApIpv4("192.168.1.0"), 24, hasGateway = false))
        assertFalse(isDirectIpv4Route(target, requireApIpv4("0.0.0.0"), 0, hasGateway = false))
        assertFalse(isDirectIpv4Route(target, subnet, 24, hasGateway = true))
        assertFalse(isDirectIpv4Route(target, InetAddress.getByAddress(ByteArray(16)), 64, hasGateway = false))
        assertTrue(isDirectIpv4Route(target, requireApIpv4("192.168.4.0"), 30, hasGateway = false))
        assertFalse(isDirectIpv4Route(target, requireApIpv4("192.168.4.4"), 30, hasGateway = false))
    }

    @Test
    fun `default connector refuses AP routing instead of falling back to a default network`() = runBlocking {
        try {
            DefaultTcpSocketConnector().connect(endpoint, 500L)
            fail("AP connection silently used the default network")
        } catch (expected: IllegalStateException) {
            assertEquals(AP_WIFI_REQUIRED_MESSAGE, expected.message)
        }
    }

    @Test
    fun `connect failure closes socket and network subscription exactly once`() = runBlocking {
        val releases = AtomicInteger()
        val failing = object : Socket() {
            override fun connect(endpoint: SocketAddress?, timeout: Int) {
                assertEquals(800, timeout)
                throw IOException("connection refused")
            }
        }
        val connection = TcpSocketConnection(failing) { releases.incrementAndGet() }
        try {
            connectTcpSocket(endpoint, 800L, Dispatchers.IO) { connection }
            fail("expected connect failure")
        } catch (expected: IOException) {
            assertEquals("connection refused", expected.message)
        }
        connection.close()
        assertTrue(failing.isClosed)
        assertEquals(1, releases.get())
    }

    @Test
    fun `cancelling an in-progress connection promptly closes socket and network subscription`() = runBlocking {
        val socket = BlockingConnectSocket()
        val releases = AtomicInteger()
        val connecting = async(Dispatchers.IO) {
            connectTcpSocket(endpoint, 60_000L, Dispatchers.IO) {
                TcpSocketConnection(socket) { releases.incrementAndGet() }
            }
        }
        assertTrue(withContext(Dispatchers.IO) { socket.started.await(2, TimeUnit.SECONDS) })
        withTimeout(2_000L) { connecting.cancelAndJoin() }
        assertTrue(socket.isClosed)
        assertEquals(1, releases.get())
    }

    @Test
    fun `transport disconnect cancels a pending socket attempt and unregisters network`() = runBlocking {
        val socket = BlockingConnectSocket()
        val releases = AtomicInteger()
        val transport = TcpTransport(
            scanner = NetworkScanner { _, _ -> emptyList() },
            connector = TcpSocketConnector { requested, timeout ->
                assertEquals(endpoint, requested)
                connectTcpSocket(requested, timeout, Dispatchers.IO) {
                    TcpSocketConnection(socket) { releases.incrementAndGet() }
                }
            },
        )
        val connecting = async(Dispatchers.IO) { transport.connect(endpoint, 60_000L) }
        assertTrue(withContext(Dispatchers.IO) { socket.started.await(2, TimeUnit.SECONDS) })
        withTimeout(2_000L) { transport.disconnect("取消 AP 连接") }
        assertTrue(connecting.isCancelled)
        assertTrue(socket.isClosed)
        assertEquals(1, releases.get())
        assertSame(TransportConnectionState.Disconnected, transport.connectionState.value)
    }

    @Test
    fun `old Wi-Fi callback cannot close or fail a new TCP session`() = runBlocking {
        ServerSocket(0).use { server ->
            val firstSocket = Socket("127.0.0.1", server.localPort)
            val firstPeer = server.accept()
            val secondSocket = Socket("127.0.0.1", server.localPort)
            val secondPeer = server.accept()
            val firstReleases = AtomicInteger()
            val secondReleases = AtomicInteger()
            val first = TcpSocketConnection(firstSocket) { firstReleases.incrementAndGet() }
            val second = TcpSocketConnection(secondSocket) { secondReleases.incrementAndGet() }
            var calls = 0
            val transport = TcpTransport(
                scanner = NetworkScanner { _, _ -> emptyList() },
                connector = TcpSocketConnector { requested, timeout ->
                    assertEquals(endpoint, requested)
                    assertEquals(800L, timeout)
                    if (calls++ == 0) first else second
                },
            )
            try {
                transport.connect(endpoint, 800L)
                transport.connect(endpoint, 800L)
                first.failAndClose("旧 Wi-Fi 已丢失")
                transport.send("test".toByteArray())
                secondPeer.soTimeout = 2_000
                val received = ByteArray(4)
                assertEquals(4, secondPeer.getInputStream().read(received))
                assertEquals("test", received.decodeToString())
                assertTrue(transport.connectionState.value is TransportConnectionState.Connected)
                assertFalse(second.isClosed)
                assertEquals(1, firstReleases.get())
                assertEquals(0, secondReleases.get())
            } finally {
                transport.disconnect("test")
                firstPeer.close()
                secondPeer.close()
            }
            assertEquals(1, secondReleases.get())
        }
    }

    @Test
    fun `registration is reclaimed when network is lost before register returns`() {
        val unregisters = AtomicInteger()
        val registration = NetworkCallbackRegistration { unregisters.incrementAndGet() }
        registration.close()
        assertEquals(0, unregisters.get())
        registration.markRegistered()
        registration.close()
        assertEquals(1, unregisters.get())
    }

    @Test
    fun `closing a registered callback repeatedly only unregisters once`() {
        val unregisters = AtomicInteger()
        val registration = NetworkCallbackRegistration { unregisters.incrementAndGet() }
        registration.markRegistered()
        registration.close()
        registration.close()
        assertEquals(1, unregisters.get())
    }

    private class BlockingConnectSocket : Socket() {
        val started = CountDownLatch(1)
        private val released = CountDownLatch(1)

        override fun connect(endpoint: SocketAddress?, timeout: Int) {
            started.countDown()
            if (!released.await(timeout.toLong(), TimeUnit.MILLISECONDS)) throw IOException("connect timeout")
            throw IOException("socket closed")
        }

        override fun close() {
            super.close()
            released.countDown()
        }
    }
}
