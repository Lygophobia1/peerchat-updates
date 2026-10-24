package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class VoiceStartRollbackTest {
    private val peer="a".repeat(64)
    private val id="b".repeat(32)
    private val consent=VoiceConsent("gesture",peer,id,false)
    private val active=VoiceSnapshot(active=true,available=true,peer=peer,id=id,lifetime=7)
    @Test fun failureAfterNativeStartEndsOnlyTheMatchingCall(){
        val ended=mutableListOf<CallTarget>()
        rollbackVoiceStart(consent,null,{active}){ended+=it}
        assertEquals(listOf(CallTarget(peer,id,7)),ended)
        for(other in listOf(active.copy(active=false),active.copy(peer="c".repeat(64)),active.copy(id="d".repeat(32))))
            rollbackVoiceStart(consent,null,{other}){fail("Replacement call must survive")}
    }
    @Test fun delayedAnswerFailureCannotEndANewerLifetime(){
        val answer=VoiceCallTarget("owner",peer,id,6)
        rollbackVoiceStart(consent.copy(answer=true),answer,{active}){fail("New lifetime must survive")}
        var ended=false
        rollbackVoiceStart(consent.copy(answer=true),answer,{active.copy(lifetime=6)}){ended=true}
        assertTrue(ended)
    }
}
