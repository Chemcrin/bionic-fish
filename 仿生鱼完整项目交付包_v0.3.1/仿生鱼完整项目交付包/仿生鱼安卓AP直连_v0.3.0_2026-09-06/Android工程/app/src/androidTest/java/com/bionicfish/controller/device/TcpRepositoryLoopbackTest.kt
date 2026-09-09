package com.bionicfish.controller.device

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.bionicfish.controller.protocol.ControlInput
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.Turn
import com.bionicfish.controller.settings.AppSettings
import com.bionicfish.controller.settings.InMemorySettingsStore
import com.bionicfish.controller.transport.BluetoothCapabilities
import com.bionicfish.controller.transport.BluetoothCapabilityProvider
import com.bionicfish.controller.transport.MockTransport
import com.bionicfish.controller.transport.TcpTransport
import com.bionicfish.controller.transport.TransportEndpoint
import com.bionicfish.controller.transport.TransportFactory
import com.bionicfish.controller.transport.TransportKind
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Android-local TCP software loopback; this does not validate ESP/AP or physical actuators. */
@RunWith(AndroidJUnit4::class)
class TcpRepositoryLoopbackTest {
    @Test
    fun realTcpHandshakeControlsAndStopSurvivePeerDisconnect() = runBlocking {
        val listener = withContext(Dispatchers.IO) {
            ServerSocket(0, 1, InetAddress.getByName("127.0.0.1")).apply { soTimeout = 3_000 }
        }
        val jobs = SupervisorJob()
        val scope = CoroutineScope(jobs + Dispatchers.IO)
        val peer = AtomicReference<Socket?>(null)
        val received = ConcurrentLinkedQueue<String>()
        val tcp = TcpTransport()
        val settings = AppSettings(
            transportKind = TransportKind.TCP, wifiHost = "127.0.0.1", wifiPort = listener.localPort,
            connectTimeoutMillis = 2_000L, handshakeTimeoutMillis = 2_000L, reconnectEnabled = false,
        )
        val repository = DefaultBionicFishRepository(
            InMemorySettingsStore(settings),
            TransportFactory { kind -> assertEquals(TransportKind.TCP, kind); tcp },
            BluetoothCapabilityProvider { BluetoothCapabilities(false, false) }, scope,
        )
        val bridge = scope.async {
            listener.accept().use { socket ->
                peer.set(socket)
                socket.soTimeout = 3_000
                socket.tcpNoDelay = true
                val model = MockTransport(responseDelayMillis = 0L)
                model.connect(TransportEndpoint("mock://tcp-loopback"), 500L)
                val replies = launch(start = CoroutineStart.UNDISPATCHED) {
                    model.incomingBytes.collect { bytes ->
                        socket.getOutputStream().write(bytes)
                        socket.getOutputStream().flush()
                    }
                }
                try {
                    val reader = socket.getInputStream().bufferedReader(Charsets.US_ASCII)
                    while (isActive) {
                        val line = reader.readLine() ?: break
                        received += line
                        // Only TCP line framing here; the production Mock handles the V1 protocol.
                        model.send((line + "\n").toByteArray(Charsets.US_ASCII))
                    }
                } finally {
                    replies.cancel()
                    model.disconnect("loopback bridge closed")
                }
            }
        }
        try {
            withTimeout(15_000L) {
                repository.state.first { it.settings == settings }
                repository.scan()
                repository.connect(repository.state.value.devices.single().id)
                assertEquals(HandshakeStatus.VERIFIED_V1_COMPATIBLE, repository.state.value.connection.handshakeStatus)
                assertTrue(received.first().contains("move=S,turn=C,step_speed=60,servo=0"))
                repository.state.first { it.telemetry.snapshot?.stepCommandedOn == false }

                repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, servoDegrees = -10))
                repository.state.first {
                    it.telemetry.snapshot?.let { sample -> sample.stepCommandedOn == true && sample.servoDegrees == -10 } == true
                }
                assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
                repository.sendControl(ControlInput(Move.FORWARD, Turn.RIGHT, servoDegrees = 20))
                repository.state.first { it.telemetry.snapshot?.servoDegrees == 20 }
                repository.sendSafeStop("loopback stop")
                repository.state.first {
                    it.telemetry.snapshot?.let { sample -> sample.stepCommandedOn == false && sample.servoDegrees == 0 } == true
                }
                val afterStop = received.size
                withTimeout(2_000L) { repository.state.first { received.size > afterStop } }
                assertTrue(received.toList().drop(afterStop).all { it.contains("move=S,turn=C") })

                peer.get()!!.close()
                withTimeout(3_000L) {
                    repository.state.first { it.connection.phase == RepositoryConnectionPhase.LOST }
                }
                delay(settings.heartbeatPeriodMillis + 100L)
                val sentAfterLoss = repository.state.value.sentFrameCount
                repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, servoDegrees = 0))
                assertEquals(sentAfterLoss, repository.state.value.sentFrameCount)
                assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)
                assertEquals(false, repository.state.value.telemetry.snapshot?.stepCommandedOn)
                assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
                assertTrue(received.all { it.contains("step_speed=60") })
                assertTrue(received.toList().drop(afterStop).all { it.contains("move=S,turn=C") })
            }
        } finally {
            peer.getAndSet(null)?.close()
            listener.close()
            withContext(NonCancellable) {
                withTimeout(3_000L) {
                    tcp.disconnect("loopback cleanup")
                    jobs.cancelAndJoin()
                    bridge.join()
                }
            }
        }
    }
}
