package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class ServerConfigurationTest {
    @Test fun configurationReopensAndCorruptionFailsClosed() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val profile="test_${UUID.randomUUID()}"
        val state=ProfileSecurity(context,profile)
        try {
            val expected=ServerEndpoint("server.example",8443,"f".repeat(64))
            val store=AndroidServerConfigurationStore(context,profile)
            assertNull(store.load());store.save(expected)
            repeat(32){assertEquals(expected,AndroidServerConfigurationStore(context,profile).load())}
            val file=File(state.directory,"server-configuration.pcr")
            val original=file.readBytes()
            assertFalse(original.toString(Charsets.UTF_8).contains(expected.host))
            file.writeBytes(original.copyOf().also{it[it.lastIndex]=(it.last().toInt() xor 1).toByte()})
            assertThrows(Exception::class.java){AndroidServerConfigurationStore(context,profile).load()}
            file.writeBytes(original.copyOf(16))
            assertThrows(Exception::class.java){AndroidServerConfigurationStore(context,profile).load()}
            file.writeBytes(original);assertEquals(expected,store.load())
            assertFalse(state.store().containsAlias(state.identityAlias)) // No extra identity.
        }finally {
            check(profile.startsWith("test_"))
            state.store().deleteEntry(state.storageAlias)
            check(!state.directory.exists()||state.directory.deleteRecursively())
        }
    }
}
