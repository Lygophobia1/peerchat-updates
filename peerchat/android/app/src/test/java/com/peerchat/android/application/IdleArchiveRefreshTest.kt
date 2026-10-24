package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class IdleArchiveRefreshTest {
    private val idle=ArchiveSyncProgress(enabled=false,authenticationGeneration=7)
    private fun skip(p:ArchiveSyncProgress=idle,initial:Boolean=false,pending:Boolean=false,serving:Boolean=false)=
        maySkipIdleArchiveRefresh(p,7,initial,pending,serving)
    @Test fun onlyFreshDisabledIdleStateCanSleep() {
        assertTrue(skip())
        listOf(idle.copy(enabled=true),idle.copy(running=true),idle.copy(projectionFresh=false),
            idle.copy(authenticationGeneration=6),idle.copy(providedQueued=1),idle.copy(providers=listOf("source")))
            .forEach{assertFalse(skip(it))}
    }
    @Test fun enrollmentAndNewWorkAlwaysRefresh() {
        assertFalse(skip(initial=true));assertFalse(skip(pending=true));assertFalse(skip(serving=true))
    }
}
