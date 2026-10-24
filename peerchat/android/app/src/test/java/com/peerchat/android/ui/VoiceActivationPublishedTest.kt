package com.peerchat.android.ui

import com.peerchat.android.application.VoiceSnapshot
import org.junit.Assert.*
import org.junit.Test

class VoiceActivationPublishedTest {
    @Test fun ringingDoesNotCompletePendingAnswer(){
        val ringing=VoiceSnapshot(active=true,direction=1,state=2)
        assertFalse(voiceActivationPublished(ringing))
        assertFalse(voiceActivationPublished(ringing.copy(controlAvailable=false)))
        assertTrue(voiceActivationPublished(ringing.copy(state=4)))
        assertTrue(voiceActivationPublished(ringing.copy(state=5)))
        assertTrue(voiceActivationPublished(ringing.copy(platformDiagnostic="Call preparation failed")))
        assertTrue(voiceActivationPublished(ringing.copy(direction=0,state=1)))
        assertFalse(voiceActivationPublished(VoiceSnapshot()))
    }
}
