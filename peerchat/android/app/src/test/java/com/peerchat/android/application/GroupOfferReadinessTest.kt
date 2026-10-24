package com.peerchat.android.application

import org.json.JSONObject
import org.json.JSONArray
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class GroupOfferReadinessTest {
    private fun offer(phase:String)=groupAttachmentSnapshots(JSONObject().put("items",JSONArray().put(JSONObject()
        .put("id","a".repeat(32)).put("name","Encrypted attachment").put("size",0).put("transferSize",0)
        .put("sending",false).put("state",phase).put("plaintextVerified",false).put("path","")
        .put("recipients",JSONArray()).put("done",0).put("time",1).put("error","")
        .put("senderPeer","b".repeat(64)).put("group","c".repeat(32)))).toString()).single()
    @Test fun encryptedEnvelopeIsVisibleButCannotBeAcceptedBeforeValidation() {
        val pending=offer("verifying_offer")
        assertFalse(pending.awaitingAcceptance);assertFalse(pending.complete)
        assertEquals("Verifying encrypted offer",pending.cachePhase)
        assertTrue(offer("available").awaitingAcceptance)
    }
}
