package com.bionicfish.controller.transport

import com.bionicfish.controller.protocol.AsciiFrameDecoder
import com.bionicfish.controller.protocol.AsciiProtocol
import com.bionicfish.controller.protocol.ControlCommand
import com.bionicfish.controller.protocol.DecodeResult
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.ProtocolFrame
import com.bionicfish.controller.protocol.StepSpeed
import com.bionicfish.controller.protocol.Turn
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class MockTransportTest {
    @Test
    fun `mock discovery and responses use production protocol`() = runTest {
        val transport = MockTransport(0L, UnconfinedTestDispatcher(testScheduler))
        val devices = transport.scan(ScanRequest(timeoutMillis = 100L))
        assertEquals(1, devices.size)
        assertEquals(TransportKind.MOCK, devices.single().kind)

        transport.connect(devices.single().endpoint, 100L)
        // SharedFlow(replay=0) 必须先建立订阅，避免模拟器同步回包早于收集器。
        val chunks = async(start = CoroutineStart.UNDISPATCHED) {
            withTimeout(1_000L) { transport.incomingBytes.take(2).toList() }
        }
        transport.send(
            AsciiProtocol.encodeControl(
                ControlCommand(4, Move.FORWARD, Turn.RIGHT, StepSpeed.FAST, 15, Move.REVERSE),
            ),
        )
        val decoder = AsciiFrameDecoder()
        val frames = chunks.await().flatMap(decoder::feed).filterIsInstance<DecodeResult.Frame>()

        assertTrue(frames.any { it.value == ProtocolFrame.Acknowledgement(4, false) })
        val status = frames.map { it.value }.filterIsInstance<ProtocolFrame.Status>().single()
        assertNull(status.stepTargetRpm)
        assertNull(status.stepEstimatedRpm)
        assertNull(status.stepActualRpm)
        assertEquals(true, status.stepRunning)
        assertEquals(Move.FORWARD, status.motor1Direction)
        assertEquals(Move.REVERSE, status.motor2Direction)
        assertFalse(status.toString().contains("n20", ignoreCase = true))
    }

    @Test
    fun `mock accepts all dual motor directions and step_on describes only M1`() = runTest {
        val transport = MockTransport(0L, UnconfinedTestDispatcher(testScheduler))
        transport.connect(TransportEndpoint("mock://bionic-fish"), 100L)
        for (motor1 in Move.entries) {
            for (motor2 in Move.entries) {
                val frame = exchange(
                    transport,
                    "<CMD,seq=5,move=${motor1.wireValue},turn=L,step_speed=60,servo=-15,m2=${motor2.wireValue}>\n",
                ) as ProtocolFrame.Status
                assertEquals(motor1, frame.motor1Direction)
                assertEquals(motor2, frame.motor2Direction)
                assertEquals(motor1 != Move.STOP, frame.stepRunning)
                assertNull(frame.stepTargetRpm)
                assertNull(frame.stepActualRpm)
            }
        }
    }

    @Test
    fun `legacy five-field command resets M2 to stop and unknown extensions are ignored`() = runTest {
        val transport = MockTransport(0L, UnconfinedTestDispatcher(testScheduler))
        transport.connect(TransportEndpoint("mock://bionic-fish"), 100L)
        val first = exchange(transport, "<CMD,seq=1,move=F,turn=C,step_speed=60,servo=0,m2=R>\n")
            as ProtocolFrame.Status
        assertEquals(Move.REVERSE, first.motor2Direction)
        val legacy = exchange(transport, "<CMD,seq=2,move=R,turn=C,step_speed=60,servo=0,future=x>\n")
            as ProtocolFrame.Status
        assertEquals(Move.REVERSE, legacy.motor1Direction)
        assertEquals(Move.STOP, legacy.motor2Direction)
    }

    @Test
    fun `mock rejects range duplicate and servo semantic errors using production boundaries`() = runTest {
        val transport = MockTransport(0L, UnconfinedTestDispatcher(testScheduler))
        transport.connect(TransportEndpoint("mock://bionic-fish"), 100L)
        val valid = "<CMD,seq=3,move=F,turn=R,step_speed=60,servo=15,m2=R>\n"
        val invalidCommands = listOf(
            valid.replace("servo=15", "servo=16") to "E_RANGE",
            valid.replace("servo=15", "servo=-16") to "E_RANGE",
            valid.replace("turn=R", "turn=L") to "E_RANGE",
            valid.replace("m2=R", "m2=X") to "E_RANGE",
            valid.replace("m2=R", "m2=F,m2=R") to "E_DUP_FIELD",
            valid.replace("seq=3", "seq=65536") to "E_RANGE",
            valid.replace("step_speed=60", "step_speed=65") to "E_RANGE",
        )
        for ((command, errorCode) in invalidCommands) {
            val result = exchange(transport, command) as ProtocolFrame.Error
            assertEquals(errorCode, result.code)
        }
    }

    private suspend fun exchange(transport: MockTransport, command: String): ProtocolFrame =
        kotlinx.coroutines.coroutineScope {
            val result = CompletableDeferred<ProtocolFrame>()
            val collector = launch(start = CoroutineStart.UNDISPATCHED) {
                val decoder = AsciiFrameDecoder()
                transport.incomingBytes.collect { bytes ->
                    decoder.feed(bytes).filterIsInstance<DecodeResult.Frame>().forEach { frame ->
                        if (frame.value is ProtocolFrame.Status || frame.value is ProtocolFrame.Error) {
                            result.complete(frame.value)
                        }
                    }
                }
            }
            try {
                transport.send(command.toByteArray(StandardCharsets.US_ASCII))
                withTimeout(1_000L) { result.await() }
            } finally {
                collector.cancel()
            }
        }
}
