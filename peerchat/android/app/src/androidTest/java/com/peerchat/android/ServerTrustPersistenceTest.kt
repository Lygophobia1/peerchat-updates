package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Explicit operator opt-in. Never prints credentials, keys or message contents.
 * Retirement is separately gated and restricted to the superseded server pin. */
@RunWith(AndroidJUnit4::class)
class ServerTrustPersistenceTest {
    @Test fun retireOnlyApprovedObsoleteAuthorization() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("peerchat_retire_obsolete_remembered")=="true")
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val configuration=AndroidServerConfigurationStore(context)
        val before=configuration.load()
        val remembered=AndroidRememberedLoginStore(context)
        val old=remembered.load() ?: error("No obsolete authorization to retire")
        check(old.endpoint==ServerEndpoint("173.225.14.37",7443,
            "f9b982096e8a1d9ed79d97efc27f97435564466bbc860b0541fd56fa52463154")) {
            "Authorization is not the explicitly approved obsolete server binding"
        }
        remembered.clear()
        assertNull(AndroidRememberedLoginStore(context).load())
        assertEquals(before,configuration.load())
        println("TRUST_PROBE approved_obsolete_authorization_retired=PASS")
    }
    @Test fun reportExistingProtectedTrustOnly() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("peerchat_trust_probe")=="true")
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        fun report(label:String,endpoint:ServerEndpoint?) {
            println("TRUST_PROBE "+label+"="+(endpoint?.let{
                it.host+":"+it.port+" pin="+it.certificatePin
            } ?: "none"))
        }
        val saved=AndroidServerConfigurationStore(context).load()
        report("protected_server",saved)
        report("remembered_server",AndroidRememberedLoginStore(context).load()?.endpoint)
        repeat(5){assertEquals(saved,AndroidServerConfigurationStore(context).load())}
        println("TRUST_PROBE protected_record_reopen=PASS")
    }
}
