package com.bionicfish.controller.settings

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import com.bionicfish.controller.transport.TransportKind
import java.io.IOException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map

interface SettingsStore {
    val settings: Flow<AppSettings>
    suspend fun update(value: AppSettings)
}

class InMemorySettingsStore(initial: AppSettings = AppSettings()) : SettingsStore {
    private val mutableSettings = MutableStateFlow(initial.validated())
    override val settings: Flow<AppSettings> = mutableSettings
    override suspend fun update(value: AppSettings) {
        mutableSettings.value = value.validated()
    }
}

class DataStoreSettingsStore(
    private val dataStore: DataStore<Preferences>,
) : SettingsStore {
    override val settings: Flow<AppSettings> = dataStore.data
        .catch { error ->
            if (error is IOException) emit(androidx.datastore.preferences.core.emptyPreferences()) else throw error
        }
        .map(::decode)

    override suspend fun update(value: AppSettings) {
        val settings = value.validated()
        dataStore.edit { preferences ->
            preferences[Keys.TRANSPORT] = settings.transportKind.name
            preferences[Keys.WIFI_HOST] = settings.wifiHost
            preferences[Keys.WIFI_PORT] = settings.wifiPort
            preferences[Keys.AP_SSID] = settings.apSsid
            preferences[Keys.AP_HOST] = settings.apHost
            preferences[Keys.AP_PORT] = settings.apPort
            preferences[Keys.LAST_AP_DIRECT] = settings.lastDeviceApDirect
            preferences[Keys.DISCOVERY_ADDRESS] = settings.discoveryAddress
            preferences[Keys.DISCOVERY_PORT] = settings.discoveryPort
            preferences[Keys.DISCOVERY_PAYLOAD] = settings.discoveryPayload
            preferences[Keys.BT_CLASSIC_UUID] = settings.bluetoothClassicUuid
            preferences[Keys.BT_LE_UUID] = settings.bluetoothLeServiceUuid
            preferences[Keys.BT_LE_CHARACTERISTIC_UUID] = settings.bluetoothLeCharacteristicUuid
            preferences[Keys.PROTOCOL_VERSION] = settings.protocolVersionExpected
            preferences[Keys.CONNECT_TIMEOUT] = settings.connectTimeoutMillis
            preferences[Keys.SCAN_TIMEOUT] = settings.scanTimeoutMillis
            preferences[Keys.HANDSHAKE_TIMEOUT] = settings.handshakeTimeoutMillis
            preferences[Keys.COMMAND_ACK_TIMEOUT] = settings.commandAckTimeoutMillis
            preferences[Keys.CONTROL_PERIOD] = settings.controlSendPeriodMillis
            preferences[Keys.HEARTBEAT_PERIOD] = settings.heartbeatPeriodMillis
            preferences[Keys.LINK_TIMEOUT] = settings.linkTimeoutMillis
            preferences[Keys.TELEMETRY_STALE] = settings.telemetryStaleMillis
            preferences[Keys.RECONNECT_ENABLED] = settings.reconnectEnabled
            preferences[Keys.RECONNECT_DELAY] = settings.reconnectDelayMillis
            preferences[Keys.RECONNECT_ATTEMPTS] = settings.reconnectMaxAttempts
            preferences[Keys.SAFE_DISCONNECT] = settings.safeStopOnDisconnect
            preferences[Keys.SAFE_BACKGROUND] = settings.safeStopOnBackground
            preferences[Keys.SAFE_CONTROL_EXIT] = settings.safeStopOnControlExit
            preferences[Keys.CENTER_SERVO_SAFE_STOP] = settings.centerServoOnSafeStop
            preferences[Keys.REDUCE_MOTION] = settings.reduceMotion
            preferences[Keys.PRESS_HOLD] = settings.pressAndHoldToMove
            preferences[Keys.ALLOW_REVERSE] = settings.allowReverseCommand
            preferences[Keys.LAST_NAME] = settings.lastDeviceName
            preferences[Keys.LAST_ADDRESS] = settings.lastDeviceAddress
            preferences[Keys.LAST_PORT] = settings.lastDevicePort
        }
    }

    private fun decode(preferences: Preferences): AppSettings {
        val defaults = AppSettings()
        return AppSettings(
            transportKind = runCatching {
                TransportKind.valueOf(preferences[Keys.TRANSPORT] ?: defaults.transportKind.name)
            }.getOrDefault(defaults.transportKind),
            wifiHost = preferences[Keys.WIFI_HOST] ?: defaults.wifiHost,
            wifiPort = preferences[Keys.WIFI_PORT] ?: defaults.wifiPort,
            apSsid = preferences[Keys.AP_SSID] ?: defaults.apSsid,
            apHost = preferences[Keys.AP_HOST] ?: defaults.apHost,
            apPort = preferences[Keys.AP_PORT] ?: defaults.apPort,
            lastDeviceApDirect = preferences[Keys.LAST_AP_DIRECT] ?: defaults.lastDeviceApDirect,
            discoveryAddress = preferences[Keys.DISCOVERY_ADDRESS] ?: defaults.discoveryAddress,
            discoveryPort = preferences[Keys.DISCOVERY_PORT] ?: defaults.discoveryPort,
            discoveryPayload = preferences[Keys.DISCOVERY_PAYLOAD] ?: defaults.discoveryPayload,
            bluetoothClassicUuid = preferences[Keys.BT_CLASSIC_UUID] ?: defaults.bluetoothClassicUuid,
            bluetoothLeServiceUuid = preferences[Keys.BT_LE_UUID] ?: defaults.bluetoothLeServiceUuid,
            bluetoothLeCharacteristicUuid = preferences[Keys.BT_LE_CHARACTERISTIC_UUID]
                ?: defaults.bluetoothLeCharacteristicUuid,
            protocolVersionExpected = preferences[Keys.PROTOCOL_VERSION] ?: defaults.protocolVersionExpected,
            connectTimeoutMillis = preferences[Keys.CONNECT_TIMEOUT] ?: defaults.connectTimeoutMillis,
            scanTimeoutMillis = preferences[Keys.SCAN_TIMEOUT] ?: defaults.scanTimeoutMillis,
            handshakeTimeoutMillis = preferences[Keys.HANDSHAKE_TIMEOUT] ?: defaults.handshakeTimeoutMillis,
            commandAckTimeoutMillis = preferences[Keys.COMMAND_ACK_TIMEOUT]
                ?: defaults.commandAckTimeoutMillis,
            controlSendPeriodMillis = preferences[Keys.CONTROL_PERIOD] ?: defaults.controlSendPeriodMillis,
            heartbeatPeriodMillis = preferences[Keys.HEARTBEAT_PERIOD] ?: defaults.heartbeatPeriodMillis,
            linkTimeoutMillis = preferences[Keys.LINK_TIMEOUT] ?: defaults.linkTimeoutMillis,
            telemetryStaleMillis = preferences[Keys.TELEMETRY_STALE] ?: defaults.telemetryStaleMillis,
            reconnectEnabled = preferences[Keys.RECONNECT_ENABLED] ?: defaults.reconnectEnabled,
            reconnectDelayMillis = preferences[Keys.RECONNECT_DELAY] ?: defaults.reconnectDelayMillis,
            reconnectMaxAttempts = preferences[Keys.RECONNECT_ATTEMPTS] ?: defaults.reconnectMaxAttempts,
            safeStopOnDisconnect = preferences[Keys.SAFE_DISCONNECT] ?: defaults.safeStopOnDisconnect,
            safeStopOnBackground = preferences[Keys.SAFE_BACKGROUND] ?: defaults.safeStopOnBackground,
            safeStopOnControlExit = preferences[Keys.SAFE_CONTROL_EXIT] ?: defaults.safeStopOnControlExit,
            centerServoOnSafeStop = preferences[Keys.CENTER_SERVO_SAFE_STOP]
                ?: defaults.centerServoOnSafeStop,
            reduceMotion = preferences[Keys.REDUCE_MOTION] ?: defaults.reduceMotion,
            pressAndHoldToMove = preferences[Keys.PRESS_HOLD] ?: defaults.pressAndHoldToMove,
            allowReverseCommand = preferences[Keys.ALLOW_REVERSE] ?: defaults.allowReverseCommand,
            lastDeviceName = preferences[Keys.LAST_NAME] ?: defaults.lastDeviceName,
            lastDeviceAddress = preferences[Keys.LAST_ADDRESS] ?: defaults.lastDeviceAddress,
            lastDevicePort = preferences[Keys.LAST_PORT] ?: defaults.lastDevicePort,
        ).runCatching { validated() }.getOrDefault(defaults)
    }

    private object Keys {
        val TRANSPORT = stringPreferencesKey("transport")
        val WIFI_HOST = stringPreferencesKey("wifi_host")
        val WIFI_PORT = intPreferencesKey("wifi_port")
        val AP_SSID = stringPreferencesKey("ap_ssid")
        val AP_HOST = stringPreferencesKey("ap_host")
        val AP_PORT = intPreferencesKey("ap_port")
        val LAST_AP_DIRECT = booleanPreferencesKey("last_ap_direct")
        val DISCOVERY_ADDRESS = stringPreferencesKey("discovery_address")
        val DISCOVERY_PORT = intPreferencesKey("discovery_port")
        val DISCOVERY_PAYLOAD = stringPreferencesKey("discovery_payload")
        val BT_CLASSIC_UUID = stringPreferencesKey("bt_classic_uuid")
        val BT_LE_UUID = stringPreferencesKey("bt_le_uuid")
        val BT_LE_CHARACTERISTIC_UUID = stringPreferencesKey("bt_le_characteristic_uuid")
        val PROTOCOL_VERSION = intPreferencesKey("protocol_version")
        val CONNECT_TIMEOUT = longPreferencesKey("connect_timeout")
        val SCAN_TIMEOUT = longPreferencesKey("scan_timeout")
        val HANDSHAKE_TIMEOUT = longPreferencesKey("handshake_timeout")
        val COMMAND_ACK_TIMEOUT = longPreferencesKey("command_ack_timeout")
        val CONTROL_PERIOD = longPreferencesKey("control_period")
        val HEARTBEAT_PERIOD = longPreferencesKey("heartbeat_period")
        val LINK_TIMEOUT = longPreferencesKey("link_timeout")
        val TELEMETRY_STALE = longPreferencesKey("telemetry_stale")
        val RECONNECT_ENABLED = booleanPreferencesKey("reconnect_enabled")
        val RECONNECT_DELAY = longPreferencesKey("reconnect_delay")
        val RECONNECT_ATTEMPTS = intPreferencesKey("reconnect_attempts")
        val SAFE_DISCONNECT = booleanPreferencesKey("safe_disconnect")
        val SAFE_BACKGROUND = booleanPreferencesKey("safe_background")
        val SAFE_CONTROL_EXIT = booleanPreferencesKey("safe_control_exit")
        val CENTER_SERVO_SAFE_STOP = booleanPreferencesKey("center_servo_safe_stop")
        val REDUCE_MOTION = booleanPreferencesKey("reduce_motion")
        val PRESS_HOLD = booleanPreferencesKey("press_hold")
        val ALLOW_REVERSE = booleanPreferencesKey("allow_reverse")
        val LAST_NAME = stringPreferencesKey("last_name")
        val LAST_ADDRESS = stringPreferencesKey("last_address")
        val LAST_PORT = intPreferencesKey("last_port")
    }
}
