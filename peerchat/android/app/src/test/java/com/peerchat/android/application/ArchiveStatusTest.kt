package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class ArchiveStatusTest {
    @Test fun currentReaderActivityDoesNotReplaceCompletedResultAndProviderCountersAreSeparate(){
        val native:(Int,ByteArray)->ByteArray={op,_->NativeRecordWriter(31).also{w->
            w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(op.toLong());w.text("Connecting to archive");w.number(0);w.number(0);w.number(0);w.number(1);w.text("a".repeat(64));w.number(0)
            w.number(0);w.number(25);w.number(1000);w.number(2);w.number(1);w.text("PeerChat/ArchiveScanAccounting/v1");w.number(0);w.number(0)
            w.text("PeerChat/ArchiveStatus/v1");w.number(1);w.number(2);w.number(1000);w.number(22);w.number(3);w.number(236);w.text("a".repeat(64));w.number(8);w.number(1);w.text("b".repeat(64))
        }.bytes()}
        val p=ArchiveEnrollmentExecution(native){_,_->error("No status RPC")}.progress(4,2)
        assertEquals("Connecting",p.currentActivity);assertFalse(p.complete);assertTrue(p.lastResultSummary.startsWith("Partial"));assertEquals(22L,p.resultImported);assertEquals(236L,p.resultDeferred);assertEquals(8L,p.providedQueued);assertEquals(0L,p.received);assertEquals(1,p.providers.size)
        assertTrue(p.copy(projectionFresh=false).currentActivity.contains("unavailable"));assertEquals(p.lastResultSummary,p.copy(projectionFresh=false).lastResultSummary)
    }
    @Test fun partialAccountingDoesNotClaimFullCompletion(){
        val native:(Int,ByteArray)->ByteArray={op,_->NativeRecordWriter(31).also{w->
            w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(op.toLong());w.text("Partial history: 3 unresolved records");w.number(25);w.number(1);w.number(0);w.number(0);w.text("a".repeat(64));w.number(0)
            w.number(25);w.number(25);w.number(0);w.number(1);w.number(1);w.text("PeerChat/ArchiveScanAccounting/v1");w.number(3);w.number(0)
        }.bytes()}
        val p=ArchiveEnrollmentExecution(native){_,_->error("No presentation RPC")}.progress(8,1)
        assertEquals("Partial",p.displayState);assertFalse(p.complete);assertFalse(p.running);assertEquals(25L,p.received);assertEquals(3L,p.deferred);assertEquals(0L,p.rejectedBatches);assertEquals(0L,p.lastSuccessful)
    }
    @Test fun presentationSnapshotUsesCommittedCountsAndPreservesRawPermissionsWhileOff(){
        val source="a".repeat(64)
        val native:(Int,ByteArray)->ByteArray={op,_->NativeRecordWriter(31).also{w->
            w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(op.toLong())
            if(op==23){w.text("Synchronizing history");w.number(500);w.number(4);w.number(0);w.number(1);w.text(source);w.number(0);w.number(8);w.number(20);w.number(1791740000000);w.number(2);w.number(0)}
            else {assertEquals(21,op);w.number(1);w.text(source);w.number(1);w.number(0);w.text("PeerChat/ArchiveDeviceApproval/v2");w.number(1);w.text(source);repeat(6){w.number(0)};w.text("")}
        }.bytes()}
        val execution=ArchiveEnrollmentExecution(native){_,_->throw AssertionError("No network calls for presentation")}
        val progress=execution.progress(8,3);assertEquals(8L,progress.received);assertEquals(500L,progress.processed);assertEquals(3L,progress.authenticationGeneration);assertFalse(progress.enabled)
        val base=NativeRecordWriter(31).also{w->w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(3);w.number(1);w.text(source);repeat(4){w.number(0)}}.bytes()
        val row=execution.devices(8,3,base).single();assertTrue(row.grantedRead);assertFalse(row.grantedServe);assertFalse(row.mayRead)
    }
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
