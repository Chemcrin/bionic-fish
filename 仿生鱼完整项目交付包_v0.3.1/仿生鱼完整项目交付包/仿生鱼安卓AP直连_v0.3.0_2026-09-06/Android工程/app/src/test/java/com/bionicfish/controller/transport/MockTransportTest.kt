package com.bionicfish.controller.transport

import com.bionicfish.controller.protocol.AsciiFrameDecoder
import com.bionicfish.controller.protocol.AsciiProtocol
import com.bionicfish.controller.protocol.ControlCommand
import com.bionicfish.controller.protocol.DecodeResult
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.ProtocolFrame
import com.bionicfish.controller.protocol.StepSpeed
import com.bionicfish.controller.protocol.Turn
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
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
                ControlCommand(4, Move.FORWARD, Turn.RIGHT, StepSpeed.FAST, 30),
            ),
        )
        val decoder = AsciiFrameDecoder()
        val frames = chunks.await().flatMap(decoder::feed).filterIsInstance<DecodeResult.Frame>()

        assertTrue(frames.any { it.value == ProtocolFrame.Acknowledgement(4, false) })
        val status = frames.map { it.value }.filterIsInstance<ProtocolFrame.Status>().single()
        assertNull(status.stepTargetRpm)
        assertNull(status.stepEstimatedRpm)
        assertNull(status.stepActualRpm)
        assertEquals(true, status.stepCommandedOn)
        assertFalse(status.toString().contains("n20", ignoreCase = true))

        val stopChunks = async(start = CoroutineStart.UNDISPATCHED) {
            withTimeout(1_000L) { transport.incomingBytes.take(2).toList() }
        }
        transport.send(AsciiProtocol.encodeControl(ControlCommand(5, Move.STOP, Turn.CENTER, StepSpeed.SLOW, 0)))
        val stopped = stopChunks.await().flatMap(decoder::feed).filterIsInstance<DecodeResult.Frame>()
            .map { it.value }.filterIsInstance<ProtocolFrame.Status>().single()
        assertEquals(false, stopped.stepCommandedOn)
        assertNull(stopped.stepTargetRpm)
    }
}
