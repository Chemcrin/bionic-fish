package com.bionicfish.controller.settings

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.mutablePreferencesOf
import androidx.datastore.preferences.core.stringPreferencesKey
import com.bionicfish.controller.transport.TransportEndpoint
import com.bionicfish.controller.transport.TransportKind
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsStoreTest {
    @Test
    fun `legacy UDP preferences migrate without resetting AP LAN or safety choices`() = runTest {
        val preferences = mutablePreferencesOf(
            stringPreferencesKey("transport") to "UDP",
            stringPreferencesKey("wifi_host") to "192.0.2.42",
            intPreferencesKey("wifi_port") to 9123,
            stringPreferencesKey("ap_ssid") to "Tank-Fish",
            stringPreferencesKey("ap_host") to "192.168.4.8",
            intPreferencesKey("ap_port") to 9234,
            stringPreferencesKey("discovery_address") to "192.0.2.255",
            intPreferencesKey("discovery_port") to 4040,
            stringPreferencesKey("discovery_payload") to "LEGACY_DISCOVERY",
            booleanPreferencesKey("safe_disconnect") to false,
            booleanPreferencesKey("safe_background") to false,
            booleanPreferencesKey("safe_control_exit") to false,
            booleanPreferencesKey("center_servo_safe_stop") to false,
            booleanPreferencesKey("press_hold") to true,
            booleanPreferencesKey("reduce_motion") to true,
            longPreferencesKey("control_period") to 250L,
            longPreferencesKey("heartbeat_period") to 600L,
        )
        val dataStore = object : DataStore<Preferences> {
            override val data = flowOf<Preferences>(preferences)

            override suspend fun updateData(transform: suspend (Preferences) -> Preferences): Preferences =
                error("Reading migrated preferences must not require a write")
        }

        val settings = DataStoreSettingsStore(dataStore).settings.first()

        assertEquals(TransportKind.TCP, settings.transportKind)
        assertEquals("", settings.discoveryAddress)
        assertEquals(0, settings.discoveryPort)
        assertEquals("", settings.discoveryPayload)
        assertEquals(TransportEndpoint("192.0.2.42", 9123), settings.selectedEndpoint())
        assertEquals("Tank-Fish", settings.apSsid)
        assertEquals(TransportEndpoint("192.168.4.8", 9234, wifiOnly = true), settings.apEndpoint())
        assertFalse(settings.safeStopOnDisconnect)
        assertFalse(settings.safeStopOnBackground)
        assertFalse(settings.safeStopOnControlExit)
        assertFalse(settings.centerServoOnSafeStop)
        assertTrue(settings.pressAndHoldToMove)
        assertTrue(settings.reduceMotion)
        assertEquals(250L, settings.controlSendPeriodMillis)
        assertEquals(600L, settings.heartbeatPeriodMillis)
    }
}
