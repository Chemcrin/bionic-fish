package com.bionicfish.controller.protocol

import java.nio.charset.StandardCharsets
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Assert.fail
import org.junit.Test

class AsciiProtocolTest {
    @Test
    fun `encode complete dual motor target appends m2 after servo`() {
        val encoded = AsciiProtocol.encodeControl(
            ControlCommand(12, Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE),
        ).toString(StandardCharsets.US_ASCII)

        assertEquals("<CMD,seq=12,move=F,turn=L,step_speed=60,servo=-15,m2=R>\n", encoded)
        assertFalse(encoded.contains("n20", ignoreCase = true))
    }

    @Test(expected = IllegalArgumentException::class)
    fun `command rejects turn and servo mismatch`() {
        ControlCommand(1, Move.STOP, Turn.LEFT, StepSpeed.SLOW, 15)
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
        assertNull(status.stepActualRpm)
        assertNull(status.rollDegrees)
        assertEquals(-2.4, status.pitchDegrees!!, 0.0001)
        assertEquals(16L, status.faultBits)
        assertNull(status.motor1Direction)
        assertNull(status.motor2Direction)
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
    fun `firmware status with NA rpm and fault 160 is accepted as stopped`() {
        val result = AsciiProtocol.decodePayload(
            "STA,seq=1,link=1,step_rpm=NA,step_est=NA,step_actual=NA,step_on=0," +
                "servo=0,roll=1.2,pitch=-5.1,yaw=74.7,err=160",
        )
        val status = (result as DecodeResult.Frame).value as ProtocolFrame.Status

        assertEquals(1, status.sequence)
        assertTrue(status.linkAlive)
        assertNull(status.stepTargetRpm)
        assertNull(status.stepEstimatedRpm)
        assertNull(status.stepActualRpm)
        assertEquals(false, status.stepRunning)
        assertEquals(1.2, status.rollDegrees!!, 0.0001)
        assertEquals(-5.1, status.pitchDegrees!!, 0.0001)
        assertEquals(74.7, status.yawDegrees!!, 0.0001)
        assertEquals(160L, status.faultBits)
    }

    @Test
    fun `NA rpm and attitude with fault 163 remain valid while step is running`() {
        val result = AsciiProtocol.decodePayload(
            "STA,seq=65535,link=1,step_rpm=NA,step_est=NA,step_actual=NA,step_on=1," +
                "servo=-30,roll=NA,pitch=NA,yaw=NA,err=163",
        )
        val status = (result as DecodeResult.Frame).value as ProtocolFrame.Status

        assertNull(status.stepTargetRpm)
        assertNull(status.stepEstimatedRpm)
        assertNull(status.stepActualRpm)
        assertNull(status.rollDegrees)
        assertNull(status.pitchDegrees)
        assertNull(status.yawDegrees)
        assertEquals(true, status.stepRunning)
        assertEquals(163L, status.faultBits)
    }

    @Test
    fun `NA rpm without step_on has unknown running state instead of stopped`() {
        val status = decodeStatus()

        assertNull(status.stepRunning)
        assertNull(status.stepTargetRpm)
        assertNull(status.stepEstimatedRpm)
    }

    @Test
    fun `old numeric firmware status retains all speed tiers and running inference`() {
        for (rpm in listOf(0, 60, 100)) {
            val status = decodeStatus(target = rpm.toString(), estimate = rpm.toString())

            assertEquals(rpm, status.stepTargetRpm)
            assertEquals(rpm, status.stepEstimatedRpm)
            assertEquals(rpm > 0, status.stepRunning)
        }
    }

    @Test
    fun `explicit step_on is authoritative over old target rpm inference`() {
        assertEquals(false, decodeStatus(target = "60", stepOn = "0").stepRunning)
        assertEquals(true, decodeStatus(target = "0", stepOn = "1").stepRunning)
    }

    @Test
    fun `target and estimated rpm independently accept NA`() {
        val unknownTarget = decodeStatus(target = "NA", estimate = "60")
        assertNull(unknownTarget.stepTargetRpm)
        assertEquals(60, unknownTarget.stepEstimatedRpm)
        assertNull(unknownTarget.stepRunning)

        val knownTarget = decodeStatus(target = "100", estimate = "NA")
        assertEquals(100, knownTarget.stepTargetRpm)
        assertNull(knownTarget.stepEstimatedRpm)
        assertEquals(true, knownTarget.stepRunning)
    }

    @Test
    fun `only NA and integer firmware speed tiers are valid for either rpm field`() {
        for (invalid in listOf("-1", "65", "101", "60.0", "garbage", "na", "2147483648")) {
            for (field in listOf("step_rpm", "step_est")) {
                val result = AsciiProtocol.decodePayload(statusPayload().replace("$field=NA", "$field=$invalid"))
                assertEquals(
                    "$field=$invalid must be rejected",
                    ProtocolParseError.INVALID_VALUE,
                    (result as DecodeResult.Malformed).reason,
                )
            }
        }
    }

    @Test
    fun `present step_on must be exactly zero or one`() {
        for (invalid in listOf("2", "-1", "true", "NA", "01", "1.0")) {
            val result = AsciiProtocol.decodePayload(statusPayload(stepOn = invalid))
            assertEquals(ProtocolParseError.INVALID_VALUE, (result as DecodeResult.Malformed).reason)
        }
    }

    @Test
    fun `NA acceptance does not make any mandatory status field optional`() {
        val tokens = statusPayload().split(',')
        for (missing in tokens.drop(1)) {
            val result = AsciiProtocol.decodePayload(tokens.filterNot { it == missing }.joinToString(","))
            assertEquals(
                "missing ${missing.substringBefore('=')} must be rejected",
                ProtocolParseError.MISSING_FIELD,
                (result as DecodeResult.Malformed).reason,
            )
        }
    }

    @Test
    fun `wire decoder preserves ACK and split NA status for handshake`() {
        val decoder = AsciiFrameDecoder()
        val first = decoder.feed(
            "<ACK,seq=1,result=OK>\n<STA,seq=1,link=1,step_rpm=N".toByteArray(),
            nowMillis = 0L,
        )
        val second = decoder.feed(
            "A,step_est=NA,step_actual=NA,step_on=0,servo=0,roll=1.2,pitch=-5.1,yaw=74.7,err=160>\n"
                .toByteArray(),
            nowMillis = 10L,
        )

        assertEquals(ProtocolFrame.Acknowledgement(1, false), (first.single() as DecodeResult.Frame).value)
        val status = (second.single() as DecodeResult.Frame).value as ProtocolFrame.Status
        assertEquals(1, status.sequence)
        assertTrue(status.linkAlive)
        assertNull(status.stepTargetRpm)
        assertEquals(false, status.stepRunning)
        assertEquals(160L, status.faultBits)
    }

    private fun statusPayload(target: String = "NA", estimate: String = "NA", stepOn: String? = null): String =
        "STA,seq=1,link=1,step_rpm=$target,step_est=$estimate,step_actual=NA," +
            (stepOn?.let { "step_on=$it," } ?: "") +
            "servo=0,roll=0.0,pitch=0.0,yaw=0.0,err=0"

    private fun decodeStatus(target: String = "NA", estimate: String = "NA", stepOn: String? = null): ProtocolFrame.Status =
        (AsciiProtocol.decodePayload(statusPayload(target, estimate, stepOn)) as DecodeResult.Frame).value as ProtocolFrame.Status

    @Test
    fun `duplicate known field is rejected`() {
        val result = AsciiProtocol.decodePayload("ACK,seq=1,seq=2,result=OK")
        assertEquals(ProtocolParseError.DUPLICATE_FIELD, (result as DecodeResult.Malformed).reason)
    }

    @Test
    fun `both command models explicitly default motor2 to stopped and safe stop stops both`() {
        assertEquals(Move.STOP, ControlCommand(1, Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0).motor2)
        assertEquals(Move.STOP, ControlInput(move = Move.FORWARD).motor2)
        assertEquals(Move.STOP, ControlInput.SAFE_STOP.move)
        assertEquals(Move.STOP, ControlInput.SAFE_STOP.motor2)
        assertEquals(0, ControlInput.SAFE_STOP.servoDegrees)
    }

    @Test
    fun `outbound commands accept fifteen degree endpoints and reject sixteen`() {
        for (angle in listOf(-15, 15)) {
            val turn = if (angle < 0) Turn.LEFT else Turn.RIGHT
            assertEquals(angle, ControlCommand(1, Move.REVERSE, turn, StepSpeed.SLOW, angle).servoDegrees)
            assertEquals(angle, ControlInput(turn = turn, servoDegrees = angle).validated().servoDegrees)
        }
        for (angle in listOf(-30, -16, 16, 30)) {
            val turn = if (angle < 0) Turn.LEFT else Turn.RIGHT
            assertThrows(IllegalArgumentException::class.java) {
                ControlCommand(1, Move.STOP, turn, StepSpeed.SLOW, angle)
            }
            assertThrows(IllegalArgumentException::class.java) {
                ControlInput(turn = turn, servoDegrees = angle).validated()
            }
        }
    }

    @Test
    fun `legacy inbound servo thirty remains accepted independently from command range`() {
        for (angle in listOf(-30, -20, -15, 15, 20, 30)) {
            val result = AsciiProtocol.decodePayload(statusPayload().replace("servo=0", "servo=$angle"))
            assertEquals(angle, ((result as DecodeResult.Frame).value as ProtocolFrame.Status).servoDegrees)
        }
        for (angle in listOf(-31, 31)) {
            val result = AsciiProtocol.decodePayload(statusPayload().replace("servo=0", "servo=$angle"))
            assertEquals(ProtocolParseError.INVALID_VALUE, (result as DecodeResult.Malformed).reason)
        }
    }

    @Test
    fun `status parses all independent direction combinations and preserves absent legacy fields`() {
        for (motor1 in Move.entries) {
            for (motor2 in Move.entries) {
                val payload = statusPayload(stepOn = if (motor1 == Move.STOP) "0" else "1") +
                    ",m1=${motor1.wireValue},m2=${motor2.wireValue}"
                val status = (AsciiProtocol.decodePayload(payload) as DecodeResult.Frame).value as ProtocolFrame.Status
                assertEquals(motor1, status.motor1Direction)
                assertEquals(motor2, status.motor2Direction)
                assertEquals(motor1 != Move.STOP, status.stepRunning)
            }
        }
        val legacy = decodeStatus()
        assertNull(legacy.motor1Direction)
        assertNull(legacy.motor2Direction)
        val onlyMotor2 = (AsciiProtocol.decodePayload(statusPayload() + ",m2=R") as DecodeResult.Frame)
            .value as ProtocolFrame.Status
        assertNull(onlyMotor2.motor1Direction)
        assertEquals(Move.REVERSE, onlyMotor2.motor2Direction)
    }

    @Test
    fun `invalid and duplicate motor directions reject entire status`() {
        for (field in listOf("m1", "m2")) {
            for (invalid in listOf("X", "NA", "STOP", "0", "f")) {
                val result = AsciiProtocol.decodePayload(statusPayload() + ",$field=$invalid")
                assertEquals(ProtocolParseError.INVALID_VALUE, (result as DecodeResult.Malformed).reason)
            }
            val duplicate = AsciiProtocol.decodePayload(statusPayload() + ",$field=F,$field=S")
            assertEquals(ProtocolParseError.DUPLICATE_FIELD, (duplicate as DecodeResult.Malformed).reason)
        }
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
        assertTrue(Turn.RIGHT.matches(15))
        assertTrue(Turn.CENTER.matches(0))
    }
}
