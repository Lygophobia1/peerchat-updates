package com.peerchat.android.platform

import android.app.Notification
import android.app.Service
import android.content.Intent
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class TransferServiceTest {
    @Test fun processRestartDoesNotInventTransferIntent(){
        val controller=Robolectric.buildService(PeerChatTransferService::class.java).create()
        val service=controller.get()
        assertEquals(Service.START_NOT_STICKY,service.onStartCommand(null,0,1))
        assertTrue(Shadows.shadowOf(service).isStoppedBySelf)
        controller.destroy()
    }
    @Test fun invalidIntentAndSystemTimeoutStopCleanly(){
        val controller=Robolectric.buildService(PeerChatTransferService::class.java).create()
        val service=controller.get()
        assertEquals(Service.START_NOT_STICKY,service.onStartCommand(Intent().putExtra("transfer","invalid"),0,1))
        service.onTimeout(1,android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        assertTrue(Shadows.shadowOf(service).isStoppedBySelf)
        controller.destroy()
    }
    @Test fun notificationProgressAndCancellationAreVisibleWithoutContents(){
        val controller=Robolectric.buildService(PeerChatTransferService::class.java).create()
        val method=PeerChatTransferService::class.java.getDeclaredMethod("notification",Long::class.javaPrimitiveType,Long::class.javaPrimitiveType,Int::class.javaPrimitiveType).also{it.isAccessible=true}
        val notification=method.invoke(controller.get(),50L,100L,1) as Notification
        assertEquals(50,notification.extras.getInt(Notification.EXTRA_PROGRESS))
        assertFalse(notification.extras.getBoolean(Notification.EXTRA_PROGRESS_INDETERMINATE))
        assertEquals("Cancel transfers",notification.actions.single().title.toString())
        assertEquals(Notification.VISIBILITY_PRIVATE,notification.visibility)
        controller.destroy()
    }
}
