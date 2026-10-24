package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class MailboxWorkLifetimeTest {
    @Test fun stopBeforeQueuedLaneStartsCannotConsumeMailboxSchedule(){
        val owner=Owner();val scope=owner.scope();var borrowed=0
        owner.lease(false);owner.lease(true)
        assertThrows(ScopedBackendWork.Superseded::class.java){runMailboxBorrow(scope){
            borrowed++;owner.deadline=Long.MAX_VALUE;owner.inFlight=true;{}
        }}
        assertEquals(0,borrowed);assertEquals(0L,owner.deadline);assertFalse(owner.inFlight)
    }
    @Test fun stopAfterBorrowStillEntersCleanupWithoutApplyingOldReply(){
        val owner=Owner();val scope=owner.scope();var applied=false;var cleaned=0
        assertThrows(ScopedBackendWork.Superseded::class.java){runMailboxBorrow(scope){
            owner.deadline=Long.MAX_VALUE;owner.inFlight=true
            owner.lease(false);owner.lease(true)
            val script:()->Unit={try{scope.local{applied=true}}finally{owner.inFlight=false;cleaned++}}
            script
        }}
        assertFalse(applied);assertEquals(1,cleaned);assertFalse(owner.inFlight)
    }
    private class Owner {
        val gate=Any();val lifetime=MailboxWorkLifetime()
        val profile=Any();val server=Any();var session=Any();var generation=1L
        var inUse=true;var consent=true;var execution=Any()
        var deadline=0L;var inFlight=false
        fun lease(active:Boolean){inUse=active;lifetime.update(active)}
        fun scope():ScopedBackendWork {
            val identity=ScopedBackendWork.Identity(profile,server,session,7,generation,lifetime.revision)
            val script=execution
            return ScopedBackendWork(gate){inUse&&consent&&execution===script&&identity.matches(profile,server,session,7,generation,lifetime.revision)}
        }
    }
    @Test fun registrationAndFetchCommitAcrossActiveLeaseChurn() {
        val owner=Owner();var registrations=0;var fetched=0
        for(operation in listOf("registration","fetch")){
            val scope=owner.scope()
            val reply=scope.network {
                // Foreground/preparation/service lease additions, removals and
                // deadline renewal all retain active mailbox authority.
                repeat(6){owner.lease(true)}
                "accepted"
            }
            scope.local {assertEquals("accepted",reply);if(operation=="registration")registrations++ else fetched++}
        }
        assertEquals(1,registrations);assertEquals(1,fetched)
    }
    @Test fun outerLaunchAndBorrowedScriptShareStableLifetimeAndReschedule() {
        val owner=Owner();val borrowed=owner.scope()
        owner.deadline=Long.MAX_VALUE;owner.inFlight=true
        owner.lease(true)
        val launched=owner.scope()
        owner.lease(true)
        launched.local{}
        try{borrowed.network{owner.lease(true);"ready"}}
        finally{borrowed.finish{owner.deadline=0;owner.inFlight=false}}
        assertEquals(0L,owner.deadline);assertFalse(owner.inFlight)
    }
    @Test fun realStopThenResumeCannotReviveBorrowedReply() {
        val owner=Owner();val scope=owner.scope();var applied=false
        try{scope.network{owner.lease(false);owner.lease(true);"old"};scope.local{applied=true};fail()}
        catch(_:ScopedBackendWork.Superseded){}
        assertFalse(applied);assertFalse(scope.finish{applied=true})
        assertEquals("new",owner.scope().network{"new"})
    }
    @Test fun replacementSessionGenerationExecutionAndConsentRemainFenced() {
        for(change in 0..3){
            val owner=Owner();val scope=owner.scope();var applied=false
            try{scope.network{when(change){0->owner.session=Any();1->owner.generation++;2->owner.execution=Any();else->owner.consent=false};"old"};scope.local{applied=true};fail()}
            catch(_:ScopedBackendWork.Superseded){}
            assertFalse(applied);assertFalse(scope.finish{applied=true})
        }
    }
}
