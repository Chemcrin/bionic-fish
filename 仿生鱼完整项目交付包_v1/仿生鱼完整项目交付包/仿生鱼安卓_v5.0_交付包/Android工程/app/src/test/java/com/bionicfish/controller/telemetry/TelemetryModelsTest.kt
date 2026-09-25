package com.bionicfish.controller.telemetry

import com.bionicfish.controller.protocol.AsciiProtocol
import com.bionicfish.controller.protocol.DecodeResult
import com.bionicfish.controller.protocol.ProtocolFrame
import com.bionicfish.controller.protocol.Move
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TelemetryModelsTest {
    @Test
    fun `NA measurements remain absent while step_on supplies independent running state`() {
        for (running in listOf(false, true)) {
            val snapshot = status(stepOn = if (running) "1" else "0").toTelemetry(1_234L)

            assertNull(snapshot.stepTargetRpm)
            assertNull(snapshot.stepEstimatedRpm)
            assertNull(snapshot.stepActualRpm)
            assertEquals(running, snapshot.stepRunning)
            assertEquals(1_234L, snapshot.receivedAtEpochMillis)
            assertEquals(160L, snapshot.faultBits)
            assertEquals(listOf("UART_TX_DROPPED", "OLED_I2C"), snapshot.activeFaults.map { it.code })
        }
    }

    @Test
    fun `missing step_on and NA speeds remain unknown after telemetry mapping`() {
        val snapshot = status().toTelemetry(0L)

        assertNull(snapshot.stepRunning)
        assertNull(snapshot.stepTargetRpm)
        assertNull(snapshot.stepEstimatedRpm)
        assertNull(snapshot.motor1Direction)
        assertNull(snapshot.motor2Direction)
    }

    @Test
    fun `motor directions are independent and M1 stopped does not hide M2 reverse`() {
        val snapshot = status(stepOn = "0").copy(
            motor1Direction = Move.STOP,
            motor2Direction = Move.REVERSE,
        ).toTelemetry(100L)
        assertEquals(false, snapshot.stepRunning)
        assertEquals(Move.STOP, snapshot.motor1Direction)
        assertEquals(Move.REVERSE, snapshot.motor2Direction)
        assertNull(snapshot.stepActualRpm)
    }

    @Test
    fun `old numeric running inference is preserved by telemetry mapping`() {
        for (rpm in listOf(0, 60, 100)) {
            val snapshot = status(rpm = rpm.toString()).toTelemetry(0L)

            assertEquals(rpm, snapshot.stepTargetRpm)
            assertEquals(rpm, snapshot.stepEstimatedRpm)
            assertEquals(rpm > 0, snapshot.stepRunning)
        }
    }

    @Test
    fun `explicit step_on takes priority even when a numeric legacy speed differs`() {
        assertEquals(false, status(rpm = "60", stepOn = "0").toTelemetry(0L).stepRunning)
        assertEquals(true, status(rpm = "0", stepOn = "1").toTelemetry(0L).stepRunning)
    }

    private fun status(rpm: String = "NA", stepOn: String? = null): ProtocolFrame.Status {
        val payload = "STA,seq=7,link=1,step_rpm=$rpm,step_est=$rpm,step_actual=NA," +
            (stepOn?.let { "step_on=$it," } ?: "") +
            "servo=0,roll=1.2,pitch=-5.1,yaw=74.7,err=160"
        return (AsciiProtocol.decodePayload(payload) as DecodeResult.Frame).value as ProtocolFrame.Status
    }
}
