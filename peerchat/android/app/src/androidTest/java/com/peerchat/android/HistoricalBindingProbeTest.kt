package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Explicit opt-in, read-only current-pin history metadata probe.
 * No login, transport, message reads, mapping writes or key creation. */
@RunWith(AndroidJUnit4::class)
class HistoricalBindingProbeTest {
    @Test fun retainedCurrentRealmBindings(){
        val args=InstrumentationRegistry.getArguments()
        org.junit.Assume.assumeTrue(args.getString("history_binding_probe")=="true")
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val security=ProfileSecurity(context,"default")
        val before=checkNotNull(security.read("identity.public"))
        check(security.store().containsAlias(security.identityAlias)&&security.store().containsAlias(security.storageAlias))
        val endpoint=checkNotNull(AndroidServerConfigurationStore(context).load())
        val local=checkNotNull(LocalConversationContextStore(context,"default").load(endpoint))
        val refs=listOf("2c2b3ff1a09038a94c1f09bc93f225124951e8a6a901810240e0683365404866",
            "3cad98973ab84da8bcf18183f78e197a42b96d60a8c0cae6c09aa4a9f80dfbae",
            "4dfee277c24245a26e14a2dff6d962dcce1174a7330bf34f65c824133d231833",
            "7927a46cbe1c751cc65c7d041818ffa6efa35ada65af0a1b7fa5fdfbad7162af")
        val bindings=FriendIdentityIndex(context,"default",endpoint,local.account).localHistoryBindings()
        for(ref in refs){val accounts=bindings.filter{ref in it.knownPeerIds}.map{it.accountId}.distinct()
            println("HISTORY_BINDING_PROBE device=$ref current_pin_accounts="+accounts.joinToString(","))}
        assertArrayEquals(before,security.read("identity.public"))
    }
}
