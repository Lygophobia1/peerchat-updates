package com.peerchat.android.application

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class AttachmentCompatibilityTest {
    @Test fun onlineDirectDemandIsBoundedAndNeverStartsForUnverifiedOrStaleScope()=kotlinx.coroutines.test.runTest {
        var demands=0;var waits=0;var probes=0
        assertTrue(awaitAttachmentDirectRoute({++probes>1},{demands++;true},{waits++}));assertEquals(1,demands);assertEquals(1,waits)
        demands=0;waits=0;assertFalse(awaitAttachmentDirectRoute({null},{demands++;true},{waits++}));assertEquals(0,demands);assertEquals(0,waits)
        demands=0;waits=0;assertFalse(awaitAttachmentDirectRoute({false},{demands++;true},{waits++}));assertEquals(1,demands);assertEquals(250,waits)
        demands=0;waits=0;probes=0;assertFalse(awaitAttachmentDirectRoute({if(++probes==1)false else null},{demands++;true},{waits++}));assertEquals(1,demands);assertEquals(1,waits)
    }
    private fun reply(state:String)=JSONObject().put("type","cache_result").put("operation","cache_dm_prepare").put("cacheVersion",2).put("dmAttachmentVersion",1).put("state",state)
    @Test fun cacheRemainsPreferredAndOnlyExactUpgradeStateOffersChoice(){
        for((state,expected) in listOf("accepted" to AttachmentRecipientPreflight.Ready,"cache_dm_recipient_upgrade_required" to AttachmentRecipientPreflight.UpgradeRequired)){
            var calls=0;assertEquals(expected,attachmentRecipientPreflight(8,{true}){op,body->calls++;assertEquals("cache_dm_prepare",op);assertEquals(8,body.getLong("recipientAccountId"));reply(state)});assertEquals(1,calls)
        }
    }
    @Test fun securityNetworkAndOtherCacheFailuresNeverBecomeDirectChoice(){
        for(state in listOf("cache_access_denied","cache_dm_source_upgrade_required","cache_storage_unavailable","cache_expired","cache_malformed_request")){
            try{attachmentRecipientPreflight(8,{true}){_,_->reply(state)};fail(state)}catch(_:IllegalStateException){}
        }
        assertThrows(java.io.IOException::class.java){attachmentRecipientPreflight(8,{true}){_,_->throw java.io.IOException("test-only")}}
    }
    @Test fun changedOwnerOrGenerationCannotUseAnEarlierPreflight(){
        var current=true
        assertThrows(IllegalStateException::class.java){attachmentRecipientPreflight(8,{current}){_,_->current=false;reply("cache_dm_recipient_upgrade_required")}}
        var called=false;assertThrows(IllegalStateException::class.java){attachmentRecipientPreflight(8,{false}){_,_->called=true;reply("accepted")}};assertFalse(called)
    }
    @Test fun malformedOrMismatchedReplyCannotAdmitCacheOrDirect(){
        for(value in listOf(reply("accepted").put("operation","cache_dm_status"),reply("accepted").put("cacheVersion",1),reply("accepted").put("dmAttachmentVersion",0))){
            assertThrows(IllegalArgumentException::class.java){attachmentRecipientPreflight(8,{true}){_,_->value}}
        }
    }
}
