package com.peerchat.android.application

/** Disabled sync denies both reading and serving. Only the idle periodic poll
 * may sleep; initial enrollment, explicit authority hints and incoming work
 * still refresh before access. Never extend authorization for enabled work. */
internal fun maySkipIdleArchiveRefresh(progress:ArchiveSyncProgress,generation:Long,
    initial:Boolean,pending:Boolean,serving:Boolean):Boolean =
    !initial&&!pending&&!serving&&progress.projectionFresh&&
        progress.authenticationGeneration==generation&&!progress.enabled&&!progress.running&&
        progress.providers.isEmpty()&&progress.providedQueued==0L
