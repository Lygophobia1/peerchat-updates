package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.platform.ServerAccounts
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.ServerRejected
import com.peerchat.android.platform.tls.PeerTls
import com.peerchat.android.platform.tls.ServerChannel
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class DiscoveryTest {
    @Test fun physicalServerAuthentication() {
        val arguments=InstrumentationRegistry.getArguments()
        val pin=arguments.getString("serverPin") ?: ""
        assumeTrue("Run with isolated server fixture arguments",pin.length==64)
        val endpoint=ServerEndpoint("127.0.0.1",arguments.getString("serverPort")!!.toInt(),pin)
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        NativeProfile(context).use { profile ->
            val device=PeerTls.peerId(ProfileBridge.publicKey(profile.handle)!!)
            val service=ServerAccounts(endpoint,device)
            val username="android-"+UUID.randomUUID().toString().take(12)
            val password=UUID.randomUUID().toString()+"Aa!"
            val first=service.register(username,password)
            assertTrue(first.accountId>0);assertTrue(first.relayOnly)
            assertFalse(first.toString().contains(first.token))
            try { service.login(username,password);fail("Active account session replaced silently") }
            catch(expected: IllegalStateException) { assertEquals(first,service.session) }
            assertEquals(device,service.verifySession().getString("publicIdentity"))
            assertEquals(first.accountId,service.presence().getLong("accountId"))
            repeat(5) {
                service.openEvents().use { channel ->
                    channel.write(JSONObject().put("type","event_heartbeat").toString().toByteArray(),4096)
                    assertEquals(first.accountId,service.verifySession().getLong("accountId"))
                }
            }
            service.logout();assertNull(service.session)
            try { service.login(username,"wrong-password");fail("Bad credentials accepted") }
            catch(expected: ServerRejected) { assertNull(service.session) }
            assertEquals(first.accountId,service.login(username,password).accountId)
            service.presence()
            try { ServerChannel(endpoint.host,endpoint.port,"0".repeat(64),false).close();fail("Wrong pin accepted") }
            catch(expected: java.io.IOException) {
                assertTrue(generateSequence<Throwable>(expected) { it.cause }.any { it is java.security.cert.CertificateException })
            }
            try { ServerChannel(endpoint.host,endpoint.port,"",false).close();fail("Untrusted development CA accepted") }
            catch(expected: java.io.IOException) {
                assertTrue(generateSequence<Throwable>(expected) { it.cause }.any { it is java.security.cert.CertificateException })
            }
            ServerChannel(endpoint.host,endpoint.port,pin,false).use { channel ->
                try { channel.write(ByteArray(4097),4096);fail("Oversized request accepted") }
                catch(expected: java.io.IOException) { }
            }
            service.logout()
            // Normal profile/identity is never destroyed by fixture cleanup.
            assertEquals(device,PeerTls.peerId(ProfileBridge.publicKey(profile.handle)!!))
        }
    }
}
