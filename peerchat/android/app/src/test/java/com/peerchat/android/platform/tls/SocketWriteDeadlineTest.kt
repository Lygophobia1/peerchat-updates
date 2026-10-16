package com.peerchat.android.platform.tls

import org.junit.Assert.*
import org.junit.Test
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class SocketWriteDeadlineTest {
    private class StalledSocket:Socket(){
        val closedByDeadline=CountDownLatch(1)
        override fun close(){closedByDeadline.countDown()}
    }
    @Test fun blockedWriterIsInterruptedEvenWhenReaderHasNoIdleTimeout(){
        val socket=StalledSocket()
        assertThrows(SocketTimeoutException::class.java){
            ServerChannel.boundedWrite(socket,30){
                assertTrue(socket.closedByDeadline.await(2,TimeUnit.SECONDS))
                throw SocketTimeoutException("closed fixture write")
            }
        }
    }
    @Test fun completedWriteCancelsDeadlineAndDoesNotCloseLiveReader(){
        val socket=StalledSocket()
        ServerChannel.boundedWrite(socket,30){}
        assertFalse(socket.closedByDeadline.await(100,TimeUnit.MILLISECONDS))
        assertThrows(IllegalArgumentException::class.java){ServerChannel.boundedWrite(socket,0){}}
    }
}
