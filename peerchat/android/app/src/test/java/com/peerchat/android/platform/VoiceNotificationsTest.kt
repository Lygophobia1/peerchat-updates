package com.peerchat.android.platform

import android.Manifest
import android.app.Application
import android.app.NotificationManager
import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.application.VoiceSnapshot
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class VoiceNotificationsTest {
    @Test fun permissionDenialDoesNotSuppressRetryAndEndedCallRemovesAlert(){
        val context=ApplicationProvider.getApplicationContext<Application>()
        val manager=context.getSystemService(NotificationManager::class.java)
        manager.cancelAll()
        val notifications=VoiceNotifications(context)
        val incoming=VoiceSnapshot(available=true,active=true,owner="test",peer="a".repeat(64),id="b".repeat(32),lifetime=1,state=2,direction=1)
        shadowOf(context).denyPermissions(Manifest.permission.POST_NOTIFICATIONS)
        notifications.update(incoming)
        assertEquals(0,shadowOf(manager).size())
        shadowOf(context).grantPermissions(Manifest.permission.POST_NOTIFICATIONS)
        notifications.update(incoming)
        assertEquals(1,shadowOf(manager).size())
        notifications.update(incoming)
        assertEquals(1,shadowOf(manager).size())
        notifications.update(VoiceSnapshot())
        assertEquals(0,shadowOf(manager).size())
    }
}
