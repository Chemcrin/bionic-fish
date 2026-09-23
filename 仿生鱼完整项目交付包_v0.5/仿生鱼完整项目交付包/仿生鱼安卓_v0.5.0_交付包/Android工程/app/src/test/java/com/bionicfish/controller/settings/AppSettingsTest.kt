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
        assertTrue(settings.allowReverseCommand)
        assertFalse(settings.reduceMotion)
    }

    @Test
    fun `AP preset comes from supplied guide and does not alter firmware timing limits`() {
        val settings = AppSettings().validated()

        assertEquals("BionicFish-AP", settings.apSsid)
        assertEquals("192.168.4.1", settings.apHost)
        assertEquals(9000, settings.apPort)
        assertEquals(800L, settings.commandAckTimeoutMillis)
        assertEquals(1_000L, settings.linkTimeoutMillis)
        assertEquals(200L, settings.controlSendPeriodMillis)
        assertEquals(500L, settings.heartbeatPeriodMillis)
        assertEquals(1_000L, settings.reconnectDelayMillis)
        assertEquals(3, settings.reconnectMaxAttempts)
        assertFalse(settings.lastDeviceApDirect)
    }

    @Test
    fun `AP endpoint is Wi-Fi bound and independent from ordinary LAN endpoint`() {
        val settings = AppSettings(
            transportKind = TransportKind.TCP,
            wifiHost = "192.168.1.20",
            wifiPort = 8000,
            apSsid = "Fish-Custom",
            apHost = "192.168.5.1",
            apPort = 9001,
        ).validated()

        assertEquals("192.168.5.1", settings.apEndpoint().address)
        assertEquals(9001, settings.apEndpoint().port)
        assertTrue(settings.apEndpoint().wifiOnly)
        assertEquals("192.168.1.20", settings.selectedEndpoint()?.address)
        assertEquals(8000, settings.selectedEndpoint()?.port)
        assertFalse(settings.selectedEndpoint()!!.wifiOnly)
        assertNull(AppSettings(transportKind = TransportKind.TCP).selectedEndpoint())
    }

    @Test
    fun `AP settings reject malformed addresses and ports before socket connection`() {
        listOf("", "localhost", "192.168.4", "192.168.4.256", "192.168.-1.1",
            "0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255", "::1",
            "192.168.4.1:9000", "192.168.4. 1").forEach { host ->
            assertThrows("不应接受 AP 地址：$host", IllegalArgumentException::class.java) {
                AppSettings(apHost = host).validated()
            }
        }
        listOf(-1, 0, 65536).forEach { port ->
            assertThrows(IllegalArgumentException::class.java) {
                AppSettings(apPort = port).validated()
            }
        }
        assertEquals(1, AppSettings(apPort = 1).validated().apPort)
        assertEquals(65535, AppSettings(apPort = 65535).validated().apPort)
    }

    @Test
    fun `AP SSID limit counts UTF-8 bytes instead of characters`() {
        assertEquals("a".repeat(32), AppSettings(apSsid = "a".repeat(32)).validated().apSsid)
        assertEquals("鱼".repeat(10), AppSettings(apSsid = "鱼".repeat(10)).validated().apSsid)
        listOf("", "   ", "a".repeat(33), "鱼".repeat(11)).forEach { ssid ->
            assertThrows(IllegalArgumentException::class.java) {
                AppSettings(apSsid = ssid).validated()
            }
        }
    }

    @Test
    fun `remembered AP name does not relabel an unrelated LAN scan candidate`() {
        val settings = AppSettings(
            transportKind = TransportKind.TCP,
            wifiHost = "192.168.1.20",
            wifiPort = 8000,
            lastDeviceName = "AP 直连（BionicFish-AP）",
            lastDeviceApDirect = true,
        )

        assertEquals("已配置 Wi-Fi 设备", settings.scanRequest().configuredName)
        assertEquals("192.168.1.20", settings.scanRequest().configuredEndpoint?.address)
        assertFalse(settings.scanRequest().configuredEndpoint!!.wifiOnly)
    }
}
