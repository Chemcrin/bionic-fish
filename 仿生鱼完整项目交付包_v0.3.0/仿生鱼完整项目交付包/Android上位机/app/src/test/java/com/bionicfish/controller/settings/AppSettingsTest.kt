package com.bionicfish.controller.settings

import com.bionicfish.controller.transport.TransportKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test

class AppSettingsTest {
    @Test
    fun `send periods preserve firmware watchdog margin`() {
        AppSettings(
            controlSendPeriodMillis = 750L,
            heartbeatPeriodMillis = 750L,
            linkTimeoutMillis = 1_000L,
        ).validated()

        assertThrows(IllegalArgumentException::class.java) {
            AppSettings(controlSendPeriodMillis = 751L).validated()
        }
        assertThrows(IllegalArgumentException::class.java) {
            AppSettings(heartbeatPeriodMillis = 751L).validated()
        }
    }

    @Test
    fun `command acknowledgement timeout precedes link watchdog`() {
        AppSettings(commandAckTimeoutMillis = 900L, linkTimeoutMillis = 1_000L).validated()
        assertThrows(IllegalArgumentException::class.java) {
            AppSettings(commandAckTimeoutMillis = 901L).validated()
        }
        assertThrows(IllegalArgumentException::class.java) {
            AppSettings(commandAckTimeoutMillis = 800L, linkTimeoutMillis = 800L).validated()
        }
    }

    @Test
    fun `real Wi-Fi endpoint stays unavailable until address and port are confirmed`() {
        assertNull(AppSettings(transportKind = TransportKind.TCP).selectedEndpoint())
        assertEquals(
            9000,
            AppSettings(transportKind = TransportKind.TCP, wifiHost = "192.0.2.10", wifiPort = 9000)
                .selectedEndpoint()?.port,
        )
    }

    @Test(expected = IllegalArgumentException::class)
    fun `link timeout must exceed both command periods`() {
        AppSettings(
            controlSendPeriodMillis = 750L,
            heartbeatPeriodMillis = 500L,
            linkTimeoutMillis = 750L,
        ).validated()
    }

    @Test
    fun `all Bluetooth UUIDs default to unconfigured instead of invented values`() {
        val settings = AppSettings()
        assertTrue(settings.bluetoothClassicUuid.isEmpty())
        assertTrue(settings.bluetoothLeServiceUuid.isEmpty())
        assertTrue(settings.bluetoothLeCharacteristicUuid.isEmpty())
        assertTrue(settings.centerServoOnSafeStop)
        assertFalse(settings.reduceMotion)
    }
}
