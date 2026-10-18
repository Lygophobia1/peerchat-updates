package com.peerchat.android.platform

import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class AndroidQolTest {
    private val records=object:RecordProtector {
        private val key=SecretKeySpec(ByteArray(32){it.toByte()},"AES")
        override fun protect(plain:ByteArray,context:ByteArray):ByteArray {
            val c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.ENCRYPT_MODE,key);c.updateAAD(context);return c.iv+c.doFinal(plain)
        }
        override fun unprotect(blob:ByteArray,context:ByteArray):ByteArray {
            val c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.DECRYPT_MODE,key,GCMParameterSpec(128,blob.copyOfRange(0,12)));c.updateAAD(context);return c.doFinal(blob.copyOfRange(12,blob.size))
        }
    }
    private fun snapshot()=MessagingSnapshot(19,"Jonah","a".repeat(64),true,"Authenticated")
    @Test fun mailboxDefaultAndExplicitOffSurviveProtectedReopen() {
        var disk:ByteArray?=null
        fun open()=ProtectedAndroidQolStore({disk},{disk=it.copyOf()},{records})
        assertTrue(open().load().mailboxStorageAllowed)
        open().save(AndroidQolState(mailboxStorageAllowed=false))
        repeat(32){assertFalse(open().load().mailboxStorageAllowed)}
        val label="android-qol-v1".toByteArray(Charsets.US_ASCII)
        val old=org.json.JSONObject(records.unprotect(disk!!,label).toString(Charsets.UTF_8))
        old.remove("mailboxStorageAllowed")
        disk=records.protect(old.toString().toByteArray(Charsets.UTF_8),label)
        assertTrue(open().load().mailboxStorageAllowed)
        old.put("mailboxStorageAllowed","invalid")
        disk=records.protect(old.toString().toByteArray(Charsets.UTF_8),label)
        assertThrows(Exception::class.java){open().load()}
    }
    @Test fun directDefaultAndExplicitCachedModeSurviveProtectedReopenWithoutMigration() {
        var first:ByteArray?=null;var second:ByteArray?=null
        fun primary()=ProtectedAndroidQolStore({first},{first=it.copyOf()},{records})
        fun anotherProfile()=ProtectedAndroidQolStore({second},{second=it.copyOf()},{records})
        assertEquals(AttachmentSendMode.Direct,primary().load().attachmentSendMode)
        primary().save(AndroidQolState(attachmentSendMode=AttachmentSendMode.Cached))
        repeat(8){assertEquals(AttachmentSendMode.Cached,primary().load().attachmentSendMode)}
        assertEquals(AttachmentSendMode.Direct,anotherProfile().load().attachmentSendMode)
        anotherProfile().save(AndroidQolState())
        assertEquals(AttachmentSendMode.Cached,primary().load().attachmentSendMode)
        val label="android-qol-v1".toByteArray(Charsets.US_ASCII)
        val old=org.json.JSONObject(records.unprotect(first!!,label).toString(Charsets.UTF_8))
        old.remove("attachmentSendMode")
        first=records.protect(old.toString().toByteArray(Charsets.UTF_8),label)
        assertEquals(AttachmentSendMode.Direct,primary().load().attachmentSendMode)
        old.put("attachmentSendMode","Invalid")
        first=records.protect(old.toString().toByteArray(Charsets.UTF_8),label)
        assertThrows(Exception::class.java){primary().load()}
    }
    private fun conversation()=ConversationSnapshot(FriendIdentity(8,"Sammy","b".repeat(64),true),ConnectionSnapshot(ready=true),listOf(
        MessageSnapshot("c".repeat(32),"b".repeat(64),"a".repeat(64),100,1,9,2,"PRIVATE received text"),
        MessageSnapshot("d".repeat(32),"a".repeat(64),"b".repeat(64),101,2,10,2,"outgoing")))
    @Test fun uiAndWakeLeaseTheSameApplicationOwner() {
        val context=androidx.test.core.app.ApplicationProvider.getApplicationContext<android.content.Context>()
        val ui=AndroidMessagingRuntime.acquire(context);val wake=AndroidMessagingRuntime.acquire(context)
        assertSame(ui.state,wake.state)
        ui.setForeground(false);assertSame(ui.state,wake.state)
        repeat(1000){val lease=AndroidMessagingRuntime.acquire(context);assertSame(wake.state,lease.state);lease.close();lease.close()}
        ui.close();wake.close()
    }
    @Test fun wakeIsStrictOpaqueAndRepeatedHintsCoalesce() {
        assertTrue(OpaquePushWake.valid(mapOf("event" to "peerchat_pending","version" to "1")))
        assertFalse(OpaquePushWake.valid(mapOf("event" to "peerchat_pending","version" to "1","message" to "secret")))
        assertFalse(OpaquePushWake.valid(mapOf("event" to "peerchat_pending")))
        val context=androidx.test.core.app.ApplicationProvider.getApplicationContext<android.content.Context>()
        repeat(100){assertTrue(OpaquePushWake.schedule(context,mapOf("event" to "peerchat_pending","version" to "1")))}
        assertEquals(1,context.getSystemService(android.app.job.JobScheduler::class.java).allPendingJobs.count{it.id==OpaquePushWake.JOB_ID})
    }
    @Test fun notificationOnlyUsesRealIncomingHistoryAndPrivacyRules() {
        val snapshot=snapshot();val conversation=conversation()
        assertNull(planMessageNotification(snapshot,conversation,null,NotificationPreview.SenderAndMessage))
        assertNull(planMessageNotification(snapshot.copy(authenticated=false),conversation,ConversationPreference(),NotificationPreview.SenderAndMessage))
        val sender=planMessageNotification(snapshot,conversation,ConversationPreference(),NotificationPreview.SenderOnly)!!
        assertEquals("Sammy",sender.title);assertEquals("New message",sender.body);assertEquals("account:8",sender.conversationId);assertEquals(1,sender.count)
        assertFalse(sender.body.contains("PRIVATE"))
        val hidden=planMessageNotification(snapshot,conversation,ConversationPreference(),NotificationPreview.HideContent)!!
        assertEquals("PeerChat",hidden.title);assertFalse(hidden.body.contains("Sammy"));assertFalse(hidden.body.contains("PRIVATE"))
        assertEquals("PRIVATE received text",planMessageNotification(snapshot,conversation,ConversationPreference(),NotificationPreview.SenderAndMessage)!!.body)
        assertNull(planMessageNotification(snapshot,conversation,ConversationPreference(notifiedSequence=9),NotificationPreview.SenderOnly))
        assertNull(planMessageNotification(snapshot,conversation,ConversationPreference(readSequence=9),NotificationPreview.SenderOnly))
    }
    @Test fun draftsAndReadNotificationCursorsReopenWithoutHistoryDuplication() {
        var disk:ByteArray?=null
        fun open()=ProtectedAndroidQolStore({disk},{disk=it.copyOf()},{records})
        val endpoint=ServerEndpoint("server.example",7443,"e".repeat(64))
        val key=conversationPreferenceKey(accountPreferenceKey(endpoint,19),8)
        assertEquals(NotificationPreview.SenderOnly,open().load().preview)
        val expected=AndroidQolState(NotificationPreview.HideContent,true,mapOf(key to ConversationPreference("PRIVATE draft 😀",7,9)))
        open().save(expected);repeat(100){assertEquals(expected,open().load())}
        assertFalse(disk!!.toString(Charsets.UTF_8).contains("PRIVATE draft"))
        assertNotEquals(key,conversationPreferenceKey(accountPreferenceKey(endpoint,20),8))
        assertNotEquals(key,conversationPreferenceKey(accountPreferenceKey(endpoint.copy(certificatePin="f".repeat(64)),19),8))
        val original=disk!!.copyOf();disk=original.also{it[it.lastIndex]=(it.last().toInt() xor 1).toByte()}
        assertThrows(Exception::class.java){open().load()}
    }
    @Test fun groupPreferencesHaveSeparateProtectionAndRetainPendingHistoryBaseline(){
        var disk:ByteArray?=null
        fun group()=ProtectedAndroidQolStore({disk},{disk=it.copyOf()},{records},"android-group-qol-v1",Regex("[0-9a-f]{64}:group:[0-9a-f]{32}"))
        val key=groupPreferenceKey("a".repeat(64),"b".repeat(32))
        val expected=AndroidQolState(conversations=mapOf(key to ConversationPreference("pending draft",baselinePending=true)))
        group().save(expected);assertEquals(expected,group().load())
        assertFalse(disk!!.toString(Charsets.UTF_8).contains("pending draft"))
        assertThrows(Exception::class.java){ProtectedAndroidQolStore({disk},{},{records}).load()}
        assertThrows(Exception::class.java){group().save(AndroidQolState(conversations=mapOf(conversationPreferenceKey("a".repeat(64),8) to ConversationPreference())))}
        val full=(1..129).associate{groupPreferenceKey("a".repeat(64),it.toString(16).padStart(32,'0')) to ConversationPreference()}
        assertThrows(Exception::class.java){group().save(AndroidQolState(conversations=full))}
        assertEquals(expected,group().load())
    }
}
