package com.bionicfish.controller.telemetry

import com.bionicfish.controller.protocol.ProtocolFrame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

fun interface EpochClock {
    fun nowMillis(): Long
}

object SystemEpochClock : EpochClock {
    override fun nowMillis(): Long = System.currentTimeMillis()
}

class TelemetryTracker(
    private val scope: CoroutineScope,
    staleAfterMillis: Long,
    private val clock: EpochClock = SystemEpochClock,
) {
    private val mutableState = MutableStateFlow(TelemetryState())
    private var staleAfterMillis = staleAfterMillis
    private var freshnessJob: Job? = null
    val state: StateFlow<TelemetryState> = mutableState.asStateFlow()

    init {
        require(staleAfterMillis >= 200L)
        freshnessJob = scope.launch {
            while (isActive) {
                refreshAge()
                delay((this@TelemetryTracker.staleAfterMillis / 4L).coerceIn(50L, 500L))
            }
        }
    }

    fun accept(status: ProtocolFrame.Status) {
        val now = clock.nowMillis()
        mutableState.value = TelemetryState(
            snapshot = status.toTelemetry(now),
            freshness = DataFreshness.FRESH,
            ageMillis = 0L,
        )
    }

    fun updateStaleThreshold(value: Long) {
        require(value >= 200L)
        staleAfterMillis = value
        refreshAge()
    }

    fun clear() {
        mutableState.value = TelemetryState()
    }

    fun close() {
        freshnessJob?.cancel()
        freshnessJob = null
    }

    private fun refreshAge() {
        val current = mutableState.value
        val snapshot = current.snapshot ?: return
        val age = (clock.nowMillis() - snapshot.receivedAtEpochMillis).coerceAtLeast(0L)
        mutableState.value = current.copy(
            freshness = if (age > staleAfterMillis) DataFreshness.STALE else DataFreshness.FRESH,
            ageMillis = age,
        )
    }
}
