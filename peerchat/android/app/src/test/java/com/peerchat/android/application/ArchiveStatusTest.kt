package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class ArchiveStatusTest {
    @Test fun progressDoesNotInventConnectivityOrTotals(){
        assertEquals("Idle",ArchiveSyncProgress().displayState)
        assertEquals("Connecting",ArchiveSyncProgress(state="Connecting to archive",running=true).displayState)
        assertEquals("Available",ArchiveSyncProgress(state="Synchronizing history",running=true).sourceState)
        assertEquals("Waiting",ArchiveSyncProgress(state="Archive authorization unavailable",running=true).displayState)
        assertEquals("Paused",ArchiveSyncProgress(state="History synchronization is off").displayState)
        assertEquals("Error",ArchiveSyncProgress(state="Archive synchronization failed closed").displayState)
        val complete=ArchiveSyncProgress(state="History synchronized",complete=true,processed=425)
        assertEquals("Complete",complete.displayState);assertEquals(425L,complete.processed)
        assertEquals("Unavailable",complete.sourceState) // Completed route retires, not permanent presence.
    }
    @Test fun revocationExtensionIsBoundedAndNotInferredFromMissingCapability(){
        val peer="a".repeat(64)
        fun bytes(extension:Boolean,ids:List<String>)=NativeRecordWriter(31).also {w->
            w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(3);w.number(1)
            w.text(peer);repeat(4){w.number(0)}
            if(extension){w.text("PeerChat/ArchiveDeviceStatus/v1");w.number(ids.size.toLong());ids.forEach(w::text);w.number(0)}
        }.bytes()
        assertEquals("Not authorized",archiveDeviceSnapshots(bytes(false,emptyList())).single().authorizationState)
        assertEquals("Revoked",archiveDeviceSnapshots(bytes(true,listOf(peer))).single().authorizationState)
        try{archiveDeviceSnapshots(bytes(true,listOf("b".repeat(64))));fail()}catch(_:IllegalArgumentException){}
        try{archiveDeviceSnapshots(bytes(true,listOf(peer,peer)));fail()}catch(_:IllegalArgumentException){}
        assertEquals("Authorized",ArchiveDeviceSnapshot(peer,false,true,true,false,true).authorizationState)
        assertEquals("Authorized",ArchiveDeviceSnapshot(peer,false,true,false,false,authorized=true).authorizationState) // OFF preserves grants.
    }
}
