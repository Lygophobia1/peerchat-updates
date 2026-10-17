package com.peerchat.android.application

import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.platform.tls.ServerChannel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.net.Socket

/** Exercises the real backend lifetime seam without a server or native profile. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class BackgroundRetentionBackendTest {
    @Test fun passiveEventDrainCannotRescheduleExpiredOrReplacedOwnership() {
        assertTrue(passiveEventDrainAllowed(false,false,true,101,100))
        assertFalse(passiveEventDrainAllowed(false,false,true,100,100))
        assertFalse(passiveEventDrainAllowed(false,false,true,99,100))
        assertFalse(passiveEventDrainAllowed(false,false,false,101,100))
        assertFalse(passiveEventDrainAllowed(false,true,true,101,100))
        assertFalse(passiveEventDrainAllowed(true,false,true,101,100))
    }

    private fun field(name:String)=MessagingBackend::class.java.getDeclaredField(name).also { it.isAccessible=true }

    private fun backend():MessagingBackend {
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext())
        backend.setInUse(false)
        assertTrue(runBlocking { backend.awaitQuiescence(5_000) })
        authenticateSyntheticScope(backend)
        backend.setInUse(true)
        return backend
    }

    @Suppress("UNCHECKED_CAST")
    private fun authenticateSyntheticScope(backend:MessagingBackend) {
        (field("mutable").get(backend) as MutableStateFlow<MessagingSnapshot>).value=
            MessagingSnapshot(authenticated=true)
        field("authorizationSnapshotInstalled").setBoolean(backend,true)
    }

    private fun scope(backend:MessagingBackend)=field("operations").get(backend) as ServerChannel.OperationScope

    private fun attach(scope:ServerChannel.OperationScope,socket:Socket) {
        val method=ServerChannel.OperationScope::class.java.getDeclaredMethod("attach",Socket::class.java)
        method.isAccessible=true
        method.invoke(scope,socket)
    }

    @Test fun passiveGraceKeepsSocketAndCancelsForegroundLoopThenReturnReusesIt() {
        val backend=backend();val scope=scope(backend);val socket=Socket()
        try {
            attach(scope,socket)
            val deadline=System.nanoTime()+5_000_000_000L
            backend.setInUse(false,retainConnectionsUntilNanos=deadline)
            assertNull("The 125 ms owner loop must be canceled",field("ownerLoop").get(backend))
            assertFalse(socket.isClosed)
            assertEquals(1,scope.activeSocketCount())

            backend.setInUse(true)
            assertFalse("Foreground return should reuse the live socket",socket.isClosed)
            assertEquals(1,scope.activeSocketCount())
        } finally { socket.close();backend.close() }
    }

    @Test fun graceDeadlineClosesTheRegisteredSocket() {
        val backend=backend();val scope=scope(backend);val socket=Socket()
        try {
            attach(scope,socket)
            backend.setInUse(false,retainConnectionsUntilNanos=System.nanoTime()+50_000_000L)
            val timeout=System.nanoTime()+2_000_000_000L
            while(!socket.isClosed&&System.nanoTime()<timeout)Thread.sleep(10)
            assertTrue("The operation scope must close its socket at grace expiry",socket.isClosed)
            assertEquals(0,scope.activeSocketCount())
            backend.setInUse(false,retainConnectionsUntilNanos=0L)
        } finally { socket.close();backend.close() }
    }

    @Test fun authorizationResetRejectsAnOlderGraceDeadline() {
        val backend=backend();val scope=scope(backend);val oldSocket=Socket();val newSocket=Socket()
        try {
            val oldDeadline=System.nanoTime()+5_000_000_000L
            attach(scope,oldSocket)
            backend.setInUse(false,retainConnectionsUntilNanos=oldDeadline)
            assertFalse(oldSocket.isClosed)
            MessagingBackend::class.java.getDeclaredMethod("clearAuthorization").also { it.isAccessible=true }.invoke(backend)

            backend.setInUse(true)
            authenticateSyntheticScope(backend)
            attach(scope,newSocket)
            backend.setInUse(false,retainConnectionsUntilNanos=oldDeadline)
            assertTrue("An earlier grace must not retain a replacement scope",newSocket.isClosed)
        } finally { oldSocket.close();newSocket.close();backend.close() }
    }
}
