package com.bionicfish.controller.protocol

import java.nio.charset.StandardCharsets
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class AsciiProtocolTest {
    @Test
    fun `encode control uses current five-field firmware contract`() {
        val encoded = AsciiProtocol.encodeControl(
            ControlCommand(12, Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -30),
        ).toString(StandardCharsets.US_ASCII)

        assertEquals("<CMD,seq=12,move=F,turn=L,step_speed=60,servo=-30>\n", encoded)
        assertFalse(encoded.contains("n20", ignoreCase = true))
    }

    @Test(expected = IllegalArgumentException::class)
    fun `command rejects turn and servo mismatch`() {
        ControlCommand(1, Move.STOP, Turn.LEFT, StepSpeed.SLOW, 30)
    }

    @Test
    fun `decoder handles split and coalesced frames`() {
        val decoder = AsciiFrameDecoder()
        val first = decoder.feed("noise<ACK,seq=7,".toByteArray(), nowMillis = 0L)
        val second = decoder.feed(
            "result=OK>\n<ERR,seq=8,code=E_RANGE>\r\n".toByteArray(),
            nowMillis = 10L,
        )

        assertTrue(first.isEmpty())
        assertEquals(2, second.size)
        assertEquals(ProtocolFrame.Acknowledgement(7, false), (second[0] as DecodeResult.Frame).value)
        assertEquals(ProtocolFrame.Error(8, "E_RANGE"), (second[1] as DecodeResult.Frame).value)
    }

    @Test
    fun `decoder times out incomplete frame and recovers`() {
        val decoder = AsciiFrameDecoder()
        decoder.feed("<STA,seq=1".toByteArray(), nowMillis = 100L)

        val timeout = decoder.onTime(nowMillis = 351L, frameTimeoutMillis = 250L)
        val recovered = decoder.feed("<ACK,seq=2,result=DUP>\n".toByteArray(), nowMillis = 352L)

        assertEquals(ProtocolParseError.FRAME_TIMEOUT, timeout?.reason)
        assertEquals(ProtocolFrame.Acknowledgement(2, true), (recovered.single() as DecodeResult.Frame).value)
    }

    @Test
    fun `status parser preserves unavailable measurements and ignores unknown extension`() {
        val result = AsciiProtocol.decodePayload(
            "STA,seq=12,link=1,step_rpm=60,step_est=60,step_actual=NA," +
                "servo=30,roll=NA,pitch=-2.4,yaw=85.7,err=16,future=x",
        )
        val status = (result as DecodeResult.Frame).value as ProtocolFrame.Status

        assertEquals(12, status.sequence)
        assertEquals(60, status.stepTargetRpm)
        assertEquals(true, status.stepCommandedOn)
        assertNull(status.stepActualRpm)
        assertNull(status.rollDegrees)
        assertEquals(-2.4, status.pitchDegrees!!, 0.0001)
        assertEquals(16L, status.faultBits)
    }

    @Test
    fun `status parser rejects a non-firmware speed tier`() {
        val result = AsciiProtocol.decodePayload(
            "STA,seq=1,link=1,step_rpm=65,step_est=60,step_actual=NA," +
                "servo=0,roll=0.0,pitch=0.0,yaw=0.0,err=0",
        )

        assertEquals(ProtocolParseError.INVALID_VALUE, (result as DecodeResult.Malformed).reason)
    }

    @Test
    fun `fixed low speed status uses commanded output without inventing RPM`() {
        listOf(0, 1).forEach { on ->
            val result = AsciiProtocol.decodePayload(
                "STA,seq=2,link=1,step_rpm=NA,step_est=NA,step_actual=NA,step_on=$on," +
                    "servo=-10,roll=NA,pitch=NA,yaw=NA,err=0",
            )
            val status = (result as DecodeResult.Frame).value as ProtocolFrame.Status
            assertNull(status.stepTargetRpm)
            assertNull(status.stepEstimatedRpm)
            assertNull(status.stepActualRpm)
            assertEquals(on == 1, status.stepCommandedOn)
        }
    }

    @Test
    fun `legacy output inference needs a known target and explicit output takes precedence`() {
        listOf("0" to false, "60" to true, "100" to true, "NA" to null).forEach { (target, on) ->
            val raw = "STA,seq=3,link=1,step_rpm=$target,step_est=60,step_actual=NA," +
                "servo=0,roll=0,pitch=0,yaw=0,err=0"
            val status = (AsciiProtocol.decodePayload(raw) as DecodeResult.Frame).value as ProtocolFrame.Status
            assertEquals(on, status.stepCommandedOn)
        }
        val explicitStop = AsciiProtocol.decodePayload(
            "STA,seq=4,link=1,step_rpm=100,step_est=100,step_actual=NA,step_on=0," +
                "servo=0,roll=0,pitch=0,yaw=0,err=0",
        )
        assertEquals(false, ((explicitStop as DecodeResult.Frame).value as ProtocolFrame.Status).stepCommandedOn)
    }

    @Test
    fun `commanded output flag accepts only zero or one`() {
        listOf("2", "-1", "true", "NA").forEach { bad ->
            val result = AsciiProtocol.decodePayload(
                "STA,seq=5,link=1,step_rpm=NA,step_est=NA,step_actual=NA,step_on=$bad," +
                    "servo=0,roll=0,pitch=0,yaw=0,err=0",
            )
            assertEquals(ProtocolParseError.INVALID_VALUE, (result as DecodeResult.Malformed).reason)
        }
    }

    @Test
    fun `duplicate known field is rejected`() {
        val result = AsciiProtocol.decodePayload("ACK,seq=1,seq=2,result=OK")
        assertEquals(ProtocolParseError.DUPLICATE_FIELD, (result as DecodeResult.Malformed).reason)
    }

    @Test
    fun `all command ranges and semantic rules remain guarded`() {
        listOf(-1, 65536).forEach { badSequence ->
            try {
                ControlCommand(badSequence, Move.STOP, Turn.CENTER, StepSpeed.SLOW, 0)
                fail("seq=$badSequence should fail")
            } catch (_: IllegalArgumentException) {
                // expected
            }
        }
        assertTrue(Turn.LEFT.matches(-1))
        assertTrue(Turn.RIGHT.matches(30))
        assertTrue(Turn.CENTER.matches(0))
    }
}
