package com.peerchat.android.application

import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.platform.ProtectedRememberedLoginStore
import com.peerchat.android.platform.RecordProtector
import com.peerchat.android.platform.RememberedLogin
import com.peerchat.android.platform.RememberedLoginRepository
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.ServerRejected
import kotlinx.coroutines.runBlocking
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
class RememberedBootstrapBackendTest {
    private fun field(name:String)=MessagingBackend::class.java.getDeclaredField(name).also{it.isAccessible=true}
    private val records=object:RecordProtector {
        private val key=SecretKeySpec(ByteArray(32){it.toByte()},"AES")
        override fun protect(plain:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE,key);cipher.updateAAD(context)
            return cipher.iv+cipher.doFinal(plain)
        }
        override fun unprotect(blob:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE,key,GCMParameterSpec(128,blob.copyOfRange(0,12)))
            cipher.updateAAD(context)
            return cipher.doFinal(blob.copyOfRange(12,blob.size))
        }
    }
    @Test fun transientBootstrapFailureRetainsProtectedCredentialAndSchedulesRetryButRevocationClearsIt(){
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext(),"bootstrap-failure-test")
        backend.setInUse(false)
        assertTrue(runBlocking{backend.awaitQuiescence(5_000)})
        var disk:ByteArray?=null
        val saved=RememberedLogin(ServerEndpoint("example.test",7443,"a".repeat(64)),8,"b".repeat(64),"c".repeat(43))
        val store:RememberedLoginRepository=ProtectedRememberedLoginStore({disk},{disk=it.copyOf()},{records})
        store.save(saved)
        field("remembered").set(backend,store)
        val fail=MessagingBackend::class.java.getDeclaredMethod("failRememberedLocked",RememberedLogin::class.java,Exception::class.java)
            .also{it.isAccessible=true}
        val gate=checkNotNull(field("gate").get(backend))
        try{
            val before=System.nanoTime()/1_000_000
            synchronized(gate){fail.invoke(backend,saved,java.net.SocketTimeoutException("bootstrap"))}
            assertEquals(saved.credential,store.load()?.credential)
            assertEquals(saved.endpoint,field("rememberedRetryEndpoint").get(backend))
            assertTrue(field("rememberedRetryAt").getLong(backend)>=before+1000)
            assertFalse(disk!!.toString(Charsets.UTF_8).contains(saved.credential))

            synchronized(gate){fail.invoke(backend,saved,ServerRejected("remember_revoked"))}
            assertNull(store.load())
            assertNull(field("rememberedRetryEndpoint").get(backend))
        }finally{backend.close()}
    }
    @Test fun bootstrapLeaseChangesOnlyAcrossForegroundTransitions(){
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext(),"bootstrap-lease-test")
        try{
            val lifetime=field("bootstrapLifetime")
            val initial=lifetime.getLong(backend)
            backend.setInUse(true)
            assertEquals(initial,lifetime.getLong(backend))
            backend.setInUse(false)
            assertEquals(initial+1,lifetime.getLong(backend))
            backend.setInUse(false)
            assertEquals(initial+1,lifetime.getLong(backend))
            backend.setInUse(true)
            assertEquals(initial+2,lifetime.getLong(backend))
        }finally{backend.close()}
    }
}
