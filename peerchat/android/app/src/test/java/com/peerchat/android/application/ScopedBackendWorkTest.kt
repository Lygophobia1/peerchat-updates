package com.peerchat.android.application

import com.peerchat.android.platform.AccountSession
import com.peerchat.android.platform.ServerRejected
import org.junit.Assert.*
import org.junit.Test

class ScopedBackendWorkTest {
    @Test fun loginBootstrapFetchDoesNotHoldProfileGateAndInstallsUnderIt(){
        val gate=Any();val started=java.util.concurrent.CountDownLatch(1)
        val release=java.util.concurrent.CountDownLatch(1)
        val pool=java.util.concurrent.Executors.newSingleThreadExecutor()
        var installed=""
        try{
            val work=pool.submit{
                scopedBootstrap(ScopedBackendWork(gate){true},{
                    assertFalse("Contact fetch held the profile gate",Thread.holdsLock(gate))
                    started.countDown();check(release.await(5,java.util.concurrent.TimeUnit.SECONDS))
                    "contacts"
                },{response->
                    assertTrue("Native contact install must hold the profile gate",Thread.holdsLock(gate))
                    installed=response
                })
            }
            assertTrue(started.await(5,java.util.concurrent.TimeUnit.SECONDS))
            synchronized(gate){assertEquals("",installed)}
            release.countDown();work.get(5,java.util.concurrent.TimeUnit.SECONDS)
            assertEquals("contacts",installed)
        }finally{release.countDown();pool.shutdownNow()}
    }
    @Test fun lateBootstrapCannotInstallAcrossSessionSwitchOrForegroundCancellation(){
        for(cancel in listOf(false,true)){
            val gate=Any();val first=Any();var session=first;var active=true
            val started=java.util.concurrent.CountDownLatch(1)
            val release=java.util.concurrent.CountDownLatch(1)
            val pool=java.util.concurrent.Executors.newSingleThreadExecutor()
            var installed=false;var invalidated=false
            val scope=ScopedBackendWork(gate){session===first&&active}
            try{
                val work=pool.submit{
                    try{scopedBootstrap(scope,{
                        started.countDown();check(release.await(5,java.util.concurrent.TimeUnit.SECONDS))
                        "late contacts"
                    },{installed=true})}catch(_:ScopedBackendWork.Superseded){
                        scope.finish{invalidated=true}
                    }
                }
                assertTrue(started.await(5,java.util.concurrent.TimeUnit.SECONDS))
                synchronized(gate){if(cancel)active=false else session=Any()}
                release.countDown();work.get(5,java.util.concurrent.TimeUnit.SECONDS)
                assertFalse(installed);assertFalse(invalidated)
            }finally{release.countDown();pool.shutdownNow()}
        }
    }
    @Test fun partialContactInstallFailureIsHandledUnderOriginalGateAfterGenerationAdvance(){
        val gate=Any();var generation=7L;var cleaned=false
        val scope=ScopedBackendWork(gate){generation==7L}
        assertThrows(IllegalStateException::class.java){scopedBootstrap(scope,{"contacts"},{
            assertTrue(Thread.holdsLock(gate))
            generation++ // Native authority may advance before a later install step fails.
            throw IllegalStateException("native install failed")
        },{failure->
            assertEquals("native install failed",failure.message)
            assertTrue(Thread.holdsLock(gate))
            cleaned=true
        })}
        assertTrue(cleaned)
        assertFalse(scope.finish{fail("Partial install changed generation")})
    }
    @Test fun backgroundResumeCannotReuseAnOldScopeEvenWithTheSameAuthentication(){
        val profile=Any();val owner=Any();val session=Any();var lifetime=1L
        val identity=ScopedBackendWork.Identity(profile,owner,session,7,11,lifetime)
        val scope=ScopedBackendWork(Any()){identity.matches(profile,owner,session,7,11,lifetime)}
        assertThrows(ScopedBackendWork.Superseded::class.java){scope.network{
            lifetime++ // background
            lifetime++ // foreground, same retained connection
            "late authenticated response"
        }}
        assertFalse(scope.finish{fail("Old foreground lease must not publish")})
        val replacement=ScopedBackendWork.Identity(profile,owner,session,7,11,lifetime)
        assertTrue(replacement.matches(profile,owner,session,7,11,lifetime))
    }
    @Test fun lateOuterTickAuthFailureCannotInvalidateReplacementScope(){
        val gate=Any();var current=true;var invalidated=0
        val scope=ScopedBackendWork(gate){current};val caught=java.util.concurrent.CountDownLatch(1);val resume=java.util.concurrent.CountDownLatch(1)
        val pool=java.util.concurrent.Executors.newSingleThreadExecutor()
        try{
            val work=pool.submit{
                try{synchronized(gate){throw ServerRejected("session_invalid")}}catch(_:ServerRejected){
                    caught.countDown();check(resume.await(5,java.util.concurrent.TimeUnit.SECONDS))
                    scope.finish{invalidated++}
                }
            }
            assertTrue(caught.await(5,java.util.concurrent.TimeUnit.SECONDS));synchronized(gate){current=false}
            resume.countDown();work.get(5,java.util.concurrent.TimeUnit.SECONDS)
            assertEquals("Late failure invalidated replacement auth",0,invalidated)
        }finally{resume.countDown();pool.shutdown();pool.awaitTermination(5,java.util.concurrent.TimeUnit.SECONDS)}
    }
    @Test fun exactIdentityRejectsEqualButDifferentSessionAndEveryReplacedField(){
        val profile=Any();val owner=Any();val session=AccountSession(7,"name","test-token",true)
        val identity=ScopedBackendWork.Identity(profile,owner,session,7,11)
        assertTrue(identity.matches(profile,owner,session,7,11))
        assertFalse(identity.matches(Any(),owner,session,7,11))
        assertFalse(identity.matches(profile,Any(),session,7,11))
        assertFalse(identity.matches(profile,owner,session.copy(),7,11))
        assertFalse(identity.matches(profile,owner,session,8,11))
        assertFalse(identity.matches(profile,owner,session,7,12))
        assertFalse(identity.matches(null,owner,session,7,11))
    }
    @Test fun staleNetworkSuccessAndAuthFailureCannotReachReplacementNativeOrFailureCallback(){
        for(fail in listOf(false,true)){
            val gate=Any();var current=true;var invalidated=0;var native=0
            val scope=ScopedBackendWork(gate){current}
            val thrown=assertThrows(ScopedBackendWork.Superseded::class.java){scope.network{
                assertFalse(Thread.holdsLock(gate));synchronized(gate){current=false}
                if(fail)throw ServerRejected("session_invalid")
                byteArrayOf(1)
            }}
            assertNotNull(thrown)
            assertFalse(scope.finish{invalidated++})
            assertThrows(ScopedBackendWork.Superseded::class.java){scope.local{native++}}
            assertEquals(0,invalidated);assertEquals(0,native)
        }
    }
    @Test fun foregroundCancellationOrRevocationPreventsNextRequestWithoutRetry(){
        for(reason in listOf("foreground","revocation","restart","reset","stop")){
            val gate=Any();var active=true;var calls=0
            val scope=ScopedBackendWork(gate){active}
            scope.network{calls++;assertFalse(Thread.holdsLock(gate))}
            synchronized(gate){active=false}
            assertThrows("$reason",ScopedBackendWork.Superseded::class.java){scope.network{calls++}}
            assertEquals(1,calls)
        }
    }
    @Test fun failureHandlingIsAtomicWithCurrentScopeInsteadOfReacquiringAfterReplacement(){
        val gate=Any();var active=true;var handled=false
        val scope=ScopedBackendWork(gate){active}
        scope.finish{
            try{throw ServerRejected("session_invalid")}catch(_:ServerRejected){
                assertTrue(Thread.holdsLock(gate));handled=true;active=false
            }
        }
        assertTrue(handled);assertFalse(scope.finish{fail("Cannot apply to replaced scope")})
    }
}
