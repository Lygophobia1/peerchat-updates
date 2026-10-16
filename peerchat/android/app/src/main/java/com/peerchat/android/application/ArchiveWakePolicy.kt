package com.peerchat.android.application

/** Scheduling projection only; native History Sync owns cursors, grants and retries.
 * A stale or waiting snapshot cannot establish successful archive completion. */
internal fun archiveWakePending(progress:ArchiveSyncProgress):Boolean {
    if(!progress.projectionFresh)return true
    if(!progress.enabled||progress.activity==4||progress.activity==5||progress.displayState=="Paused")return false
    return progress.running||progress.providers.isNotEmpty()||progress.activity in 1..3||
        (!progress.complete&&progress.source.isNotEmpty())
}
