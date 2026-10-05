package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.gms.tasks.Tasks
import com.google.firebase.messaging.FirebaseMessaging
import com.peerchat.android.platform.AndroidPushRegistration
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.Socket
import java.net.InetSocketAddress
import java.util.concurrent.TimeUnit

/** Explicit opt-in live provider test. Token travels only in memory over the
 * authorized ADB loopback tunnel; never instrumentation output or a file. */
@RunWith(AndroidJUnit4::class)
class FcmDeviceTest {
    @Test fun realOpaqueWakeProviderAcceptsCurrentInstallation() {
        val args=InstrumentationRegistry.getArguments()
        val port=args.getString("fcmProbePort")?.toIntOrNull()
        assumeTrue("Explicit local FCM probe required",port!=null&&BuildConfig.FCM_CONFIGURED)
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val token=Tasks.await(FirebaseMessaging.getInstance().token,30,TimeUnit.SECONDS)
        assertTrue(token.length in 1..2048)
        AndroidPushRegistration(context).replaceFcmToken(token)
        Socket().use {socket->
            socket.connect(InetSocketAddress("127.0.0.1",port!!),10000);socket.soTimeout=30000
            val bytes=token.toByteArray(Charsets.US_ASCII)
            val output=DataOutputStream(socket.getOutputStream())
            output.writeInt(bytes.size);output.write(bytes);output.flush()
            assertEquals("Provider must accept real opaque wake",1,DataInputStream(socket.getInputStream()).readInt())
        }
    }
}
