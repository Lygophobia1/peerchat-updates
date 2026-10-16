package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class ArchiveWakePolicyTest {
    @Test fun mailboxCompletionCannotSettleActiveOrUnavailableArchive(){
        assertTrue(archiveWakePending(ArchiveSyncProgress(running=true,activity=1)))
        assertTrue(archiveWakePending(ArchiveSyncProgress(running=true,activity=2)))
        assertTrue(archiveWakePending(ArchiveSyncProgress(state="Archive source unavailable; retry pending",activity=3)))
        assertTrue(archiveWakePending(ArchiveSyncProgress(complete=true,projectionFresh=false)))
        assertTrue(archiveWakePending(ArchiveSyncProgress(state="Waiting for archive authorization",activity=0,source="a".repeat(64))))
    }
    @Test fun settledDisabledPausedAndUnauthorizedProfilesReleaseBoundedWake(){
        assertFalse(archiveWakePending(ArchiveSyncProgress()))
        assertFalse(archiveWakePending(ArchiveSyncProgress(complete=true,activity=0)))
        assertFalse(archiveWakePending(ArchiveSyncProgress(enabled=false,running=true)))
        assertFalse(archiveWakePending(ArchiveSyncProgress(activity=4,source="a".repeat(64))))
        assertFalse(archiveWakePending(ArchiveSyncProgress(activity=6,state="Archive synchronization failed closed",source="a".repeat(64))))
        assertTrue(archiveWakePending(ArchiveSyncProgress(complete=true,providers=listOf("a".repeat(64)))))
    }
    @Test fun staleCompletionAndUnavailableSourceDisplayPending(){
        assertEquals("Waiting",ArchiveSyncProgress(complete=true,projectionFresh=false).displayState)
        assertEquals("Waiting",ArchiveSyncProgress(complete=true,state="Archive source unavailable; retry pending").displayState)
    }
}
