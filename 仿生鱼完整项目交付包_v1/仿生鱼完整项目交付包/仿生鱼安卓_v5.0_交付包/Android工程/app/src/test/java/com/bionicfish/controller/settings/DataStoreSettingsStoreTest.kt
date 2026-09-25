package com.bionicfish.controller.settings

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.preferencesOf
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DataStoreSettingsStoreTest {
    @Test
    fun `new install without stored reverse preference enables both motor reverse controls`() = runTest {
        val store = DataStoreSettingsStore(TestPreferencesDataStore())
        assertTrue(store.settings.first().allowReverseCommand)
    }

    @Test
    fun `upgrade retains explicit reverse disabled preference`() = runTest {
        val store = DataStoreSettingsStore(
            TestPreferencesDataStore(preferencesOf(booleanPreferencesKey("allow_reverse") to false)),
        )
        assertFalse(store.settings.first().allowReverseCommand)
        store.update(store.settings.first().copy(reduceMotion = true))
        assertFalse(store.settings.first().allowReverseCommand)
        assertTrue(store.settings.first().reduceMotion)
    }

    @Test
    fun `upgrade retains explicit reverse enabled preference`() = runTest {
        val store = DataStoreSettingsStore(
            TestPreferencesDataStore(preferencesOf(booleanPreferencesKey("allow_reverse") to true)),
        )
        assertTrue(store.settings.first().allowReverseCommand)
    }

    @Test
    fun `invalid unrelated legacy preference cannot reenable explicit disabled reverse`() = runTest {
        val store = DataStoreSettingsStore(
            TestPreferencesDataStore(
                preferencesOf(
                    booleanPreferencesKey("allow_reverse") to false,
                    longPreferencesKey("control_period") to 9_999L,
                ),
            ),
        )
        val decoded = store.settings.first()
        assertFalse(decoded.allowReverseCommand)
        assertEquals(AppSettings().controlSendPeriodMillis, decoded.controlSendPeriodMillis)
    }

    @Test
    fun `explicit user changes to reverse preference survive stored roundtrip`() = runTest {
        val dataStore = TestPreferencesDataStore()
        val store = DataStoreSettingsStore(dataStore)
        store.update(AppSettings(allowReverseCommand = false))
        assertFalse(DataStoreSettingsStore(dataStore).settings.first().allowReverseCommand)
        store.update(AppSettings(allowReverseCommand = true))
        assertTrue(DataStoreSettingsStore(dataStore).settings.first().allowReverseCommand)
    }

    private class TestPreferencesDataStore(initial: Preferences = emptyPreferences()) : DataStore<Preferences> {
        private val current = MutableStateFlow(initial)
        override val data: Flow<Preferences> = current

        override suspend fun updateData(transform: suspend (t: Preferences) -> Preferences): Preferences {
            val updated = transform(current.value)
            current.value = updated
            return updated
        }
    }
}
