package com.peerchat.android.application

import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.platform.RememberedLogin
import com.peerchat.android.platform.RememberedLoginRepository
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.util.concurrent.CompletableFuture
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class RememberedResumeBackendTest {
    private val endpoint=ServerEndpoint("example.test",7443,"a".repeat(64))
    private fun field(name:String)=MessagingBackend::class.java.getDeclaredField(name).also{it.isAccessible=true}
    private fun schedule(backend:MessagingBackend):CompletableFuture<Boolean> {
        val method=MessagingBackend::class.java.getDeclaredMethod("scheduleRememberedResumeLocked",ServerEndpoint::class.java).also{it.isAccessible=true}
        @Suppress("UNCHECKED_CAST")
        return method.invoke(backend,endpoint) as CompletableFuture<Boolean>
    }
    private fun clear(backend:MessagingBackend){
        MessagingBackend::class.java.getDeclaredMethod("clearAuthorization").also{it.isAccessible=true}.invoke(backend)
    }
    @Test fun concurrentRestoreSharesCompletionWithoutHoldingLocalGateWhileCredentialOwnerIsBusy(){
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext(),"resume-owner-test")
        val gate=checkNotNull(field("gate").get(backend))
        val credentialGate=checkNotNull(field("credentialGate").get(backend))
        val probe=Executors.newSingleThreadExecutor()
        try{
            synchronized(credentialGate){
                val first=synchronized(gate){schedule(backend)}
                val second=synchronized(gate){schedule(backend)}
                assertSame(first,second)
                assertFalse(first.isDone)
                // Production resume is blocked behind a credential transaction.
                // Local send/history admission must remain independently available.
                assertTrue(probe.submit<Boolean>{synchronized(gate){true}}.get(1,TimeUnit.SECONDS))
                synchronized(gate){clear(backend)}
                assertFalse(first.get(1,TimeUnit.SECONDS))
            }
        }finally{backend.close();probe.shutdownNow()}
    }
    @Test fun foregroundAndGenerationReplacementNeverJoinTheOldPendingResume(){
        for(changed in listOf("bootstrapLifetime","generation")){
            val backend=MessagingBackend(ApplicationProvider.getApplicationContext(),"resume-replace-$changed")
            val gate=checkNotNull(field("gate").get(backend))
            try{
                synchronized(checkNotNull(field("credentialGate").get(backend))){
                    synchronized(gate){
                        val first=schedule(backend)
                        field(changed).setLong(backend,field(changed).getLong(backend)+1)
                        val second=schedule(backend)
                        assertNotSame(first,second)
                        assertFalse(first.get(1,TimeUnit.SECONDS))
                        assertFalse(second.isDone)
                        clear(backend)
                        assertFalse(second.get(1,TimeUnit.SECONDS))
                    }
                }
            }finally{backend.close()}
        }
    }
    @Test fun originalAuthenticationFailureReachesCallerWithoutExecutionWrapper(){
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext(),"resume-error-test")
        val expected=javax.net.ssl.SSLHandshakeException("fixture certificate refusal")
        val gate=checkNotNull(field("gate").get(backend))
        try{
            synchronized(gate){
                field("localEndpoint").set(backend,endpoint)
                @Suppress("UNCHECKED_CAST")
                val state=field("mutable").get(backend) as MutableStateFlow<MessagingSnapshot>
                state.value=MessagingSnapshot(localHistoryReady=true)
                field("remembered").set(backend,object:RememberedLoginRepository {
                    override fun load():RememberedLogin?=throw expected
                    override fun save(value:RememberedLogin){fail("Unexpected credential write")}
                    override fun clear(){fail("Unexpected credential removal")}
                })
            }
            val caught=assertThrows(javax.net.ssl.SSLHandshakeException::class.java){runBlocking{backend.restoreRemembered(endpoint)}}
            assertEquals(expected.message,caught.message) // Coroutines may copy exceptions when recovering stack traces.
            assertNull(field("accounts").get(backend))
            assertNull(field("resumeRequest").get(backend))
        }finally{backend.close()}
    }
}
