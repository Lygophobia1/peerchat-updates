package com.peerchat.android.platform

import com.peerchat.android.platform.tls.BoundedRpc
import java.io.EOFException
import java.net.SocketException
import java.net.SocketTimeoutException
import java.security.cert.CertificateException
import java.util.concurrent.CancellationException
import javax.net.ssl.SSLException
import org.junit.Assert.*
import org.junit.Test

class CacheReadRetryTest {
    private class Channel:AutoCloseable {var closed=false;override fun close(){closed=true}}
    @Test fun closedCapabilityAndPreparationRecoverOnFreshResource() {
        for(operation in listOf("cache_dm_capability","cache_dm_prepare")){
            val opened=mutableListOf<Channel>();var attempts=0;var fences=0
            val owner=BoundedRpc({Channel().also{opened+=it}},{!it.closed})
            val result=retryClosedCacheRead(operation,{fences++}){
                owner.run("same-session") {if(++attempts==1)throw EOFException();"ready"}
            }
            assertEquals("ready",result);assertEquals(2,attempts);assertEquals(3,fences)
            assertEquals(2,opened.size);assertTrue(opened.first().closed);assertFalse(opened.last().closed)
            owner.close()
        }
    }
    @Test fun mutationsAndOtherReadsNeverReplay() {
        for(operation in listOf("cache_dm_register","cache_dm_admit","cache_dm_append","cache_dm_complete","cache_dm_acquire","cache_dm_publish","cache_dm_status","mailbox_fetch","cache_capability")){
            var attempts=0;val failure=EOFException()
            try{retryClosedCacheRead(operation,{}){attempts++;throw failure};fail()}catch(actual:EOFException){assertSame(failure,actual)}
            assertEquals(operation,1,attempts)
        }
    }
    @Test fun secondFailurePropagatesWithoutThirdAttempt() {
        var attempts=0;val second=SocketException("Socket closed")
        try{retryClosedCacheRead("cache_dm_prepare",{}){if(++attempts==1)throw EOFException();throw second};fail()}
        catch(actual:SocketException){assertSame(second,actual)}
        assertEquals(2,attempts)
    }
    @Test fun cancellationOrChangedSessionPreventsFreshAttempt() {
        for(cancel in listOf(false,true)){
            var current=true;var attempts=0
            try{retryClosedCacheRead("cache_dm_capability",{if(!current){if(cancel)throw CancellationException();error("session changed")}}){attempts++;current=false;throw EOFException()};fail()}
            catch(_:CancellationException){assertTrue(cancel)}catch(_:IllegalStateException){assertFalse(cancel)}
            assertEquals(1,attempts)
        }
        var attempts=0
        try{retryClosedCacheRead("cache_dm_prepare",{throw CancellationException()}){attempts++};fail()}catch(_:CancellationException){}
        assertEquals(0,attempts)
    }
    @Test fun lateScopeChangeRejectsSuccessfulReply() {
        var current=true
        try{retryClosedCacheRead("cache_dm_prepare",{check(current)}){current=false;"stale"};fail()}catch(_:IllegalStateException){}
    }
    @Test fun tlsCertificateTimeoutAndOtherSocketFailuresNeverReplay() {
        val failures=listOf(SSLException("closed"),EOFException().also{it.initCause(SSLException("closed"))},
            SocketException("Socket closed").also{it.initCause(CertificateException())},
            SocketTimeoutException(),SocketException("Connection refused"),java.io.IOException("Socket closed"),CancellationException())
        for(failure in failures){var attempts=0
            try{retryClosedCacheRead("cache_dm_capability",{}){attempts++;throw failure};fail()}
            catch(actual:Exception){assertSame(failure,actual)}
            assertEquals(1,attempts)
        }
        for(message in listOf("Socket closed","Socket is closed","Connection reset","Connection reset by peer","Broken pipe"))assertTrue(closedCacheReadTransport("cache_dm_prepare",SocketException(message)))
    }
}
