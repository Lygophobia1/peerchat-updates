package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.platform.AndroidRecipientKeyProvider
import com.peerchat.android.platform.ProfileSecurity
import java.io.File
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Isolated test_* profiles only. Auth/server acceptance is simulated here;
 * production bootstrap and real TLS mailbox regressions are separate checks. */
@RunWith(AndroidJUnit4::class)
class ScopedMailboxDeviceTest {
    @Test fun authenticatedAccountRebindingIsRejectedAndOriginalKeySurvivesRestart(){
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val name="test_${UUID.randomUUID()}";val security=ProfileSecurity(context,name)
        val provider=AndroidRecipientKeyProvider(context,name)
        org.junit.Assume.assumeTrue(provider.supported())
        val label="localhost:7443".toByteArray(Charsets.US_ASCII);val pin="a".repeat(64).toByteArray(Charsets.US_ASCII)
        val empty=NativeRecordWriter(1).also{it.number(0)}.bytes()
        var original:ByteArray?=null;var identity:ByteArray?=null
        try{
            NativeProfile(context,name).use { p ->
                identity=ProfileBridge.publicKey(p.handle)
                assertEquals(0,ReliableBridge.friends(p.handle,19,1,empty))
                original=checkNotNull(MailboxBridge.registration(p.handle,true))
                assertEquals(0,MailboxBridge.registered(p.handle,original!!))
                // The old fixture expected the same device's authenticated
                // ownership to change from account 19 to 4. Current native
                // provenance correctly rejects that, before re-enrollment.
                assertNotEquals(0,ReliableBridge.friends(p.handle,4,2,empty))
                assertNotEquals(0,MailboxBridge.configureEnrollment(p.handle,4,2,label,pin))
                assertArrayEquals(original,MailboxBridge.registration(p.handle,true))
                assertEquals(0,ReliableBridge.friends(p.handle,19,3,empty))
                assertEquals(0,MailboxBridge.configureEnrollment(p.handle,19,3,label,pin))
                assertArrayEquals(original,MailboxBridge.registration(p.handle,true))
                // A normal same-account key needs no cross-account enrollment.
                assertEquals(0,MailboxBridge.hasScopedEnrollment(p.handle))
                assertNull(MailboxBridge.reenrollmentOffer(p.handle))
                assertEquals(0,MailboxBridge.registered(p.handle,original!!))
                assertArrayEquals(identity,ProfileBridge.publicKey(p.handle));assertNull(security.store().getKey(provider.alias(1),null).encoded)
            }
            NativeProfile(context,name).use { p ->
                assertNotEquals(0,ReliableBridge.friends(p.handle,4,4,empty))
                assertEquals(0,ReliableBridge.friends(p.handle,19,5,empty))
                assertEquals(0,MailboxBridge.configureEnrollment(p.handle,19,5,label,pin))
                assertArrayEquals(original,MailboxBridge.registration(p.handle,true))
                assertNull(MailboxBridge.reenrollmentOffer(p.handle))
                assertArrayEquals(identity,ProfileBridge.publicKey(p.handle))
            }
        }finally{
            check(name.startsWith("test_"));val store=security.store()
            store.deleteEntry(provider.alias(1));store.deleteEntry(security.identityAlias);store.deleteEntry(security.storageAlias)
            val profile=File(context.noBackupFilesDir,"profiles/"+security.id.joinToString(""){"%02x".format(it)})
            check(!profile.exists()||profile.deleteRecursively());check(!security.directory.exists()||security.directory.deleteRecursively())
        }
    }
}
