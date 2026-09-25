package com.bionicfish.controller.transport

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.ServerSocket
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NetworkTransportTest {
    @Test
    fun `tcp transport exchanges bytes on loopback`() = runBlocking {
        val server = ServerSocket(0)
        val serverJob = async(Dispatchers.IO) {
            server.accept().use { peer ->
                val received = ByteArray(128)
                val count = peer.getInputStream().read(received)
                peer.getOutputStream().write("<ACK,seq=1,result=OK>\n".toByteArray())
                String(received, 0, count, StandardCharsets.US_ASCII)
            }
        }
        val transport = TcpTransport(scanner = NetworkScanner { _, _ -> emptyList() })
        transport.connect(TransportEndpoint("127.0.0.1", server.localPort), 2_000L)
        // SharedFlow 不重放，先以 UNDIDSPATCHED 进入 first() 再触发服务端回包。
        val incoming = async(start = CoroutineStart.UNDISPATCHED) {
            withTimeout(2_000L) { transport.incomingBytes.first() }
        }
        transport.send("probe\n".toByteArray())

        assertEquals("probe\n", serverJob.await())
        assertEquals("<ACK,seq=1,result=OK>\n", incoming.await().toString(StandardCharsets.US_ASCII))
        transport.disconnect("test")
        withContext(Dispatchers.IO) { server.close() }
    }

    @Test
    fun `udp transport exchanges datagrams on loopback`() = runBlocking {
        val server = DatagramSocket(0)
        val serverJob = async(Dispatchers.IO) {
            val buffer = ByteArray(128)
            val received = DatagramPacket(buffer, buffer.size)
            server.receive(received)
            val reply = "<ACK,seq=2,result=OK>\n".toByteArray()
            server.send(DatagramPacket(reply, reply.size, received.address, received.port))
            String(received.data, received.offset, received.length, StandardCharsets.US_ASCII)
        }
        val transport = UdpTransport(scanner = NetworkScanner { _, _ -> emptyList() })
        transport.connect(TransportEndpoint("127.0.0.1", server.localPort), 2_000L)
        val incoming = async(start = CoroutineStart.UNDISPATCHED) {
            withTimeout(2_000L) { transport.incomingBytes.first() }
        }
        transport.send("probe\n".toByteArray())

        assertEquals("probe\n", serverJob.await())
        assertTrue(incoming.await().toString(StandardCharsets.US_ASCII).startsWith("<ACK"))
        transport.disconnect("test")
        withContext(Dispatchers.IO) { server.close() }
    }

    @Test
    fun `blank discovery protocol never invents a probe`() = runBlocking {
        val scanner = UdpDiscoveryScanner()
        val saved = scanner.scan(
            TransportKind.TCP,
            ScanRequest(
                timeoutMillis = 100L,
                configuredEndpoint = TransportEndpoint("192.0.2.1", 1234),
            ),
        )
        assertEquals(1, saved.size)
        assertEquals(DiscoverySource.SAVED, saved.single().discoverySource)
    }
}
