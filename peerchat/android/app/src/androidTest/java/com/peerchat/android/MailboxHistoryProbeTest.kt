package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.messageSnapshots
import com.peerchat.android.application.accountHistoryPage
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Explicit opt-in, read-only characterization of the EXISTING normal profile.
 * No login, transport, mailbox receive/ACK, identity creation, key rotation,
 * message writes or destructive test hooks. Never prints message contents. */
@RunWith(AndroidJUnit4::class)
class MailboxHistoryProbeTest {
    @Test fun existingNormalHistoryByKnownDevice() {
        val args = InstrumentationRegistry.getArguments()
        val account = args.getString("history_probe_account")?.toLongOrNull()
        assumeTrue("Explicit physical history probe required", account != null && account > 0)
        val label = args.getString("history_probe_label")
        require(label == null || label.length in 1..64)
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val security = ProfileSecurity(context, "default")
        val public = checkNotNull(security.read("identity.public"))
        assertTrue(security.store().containsAlias(security.identityAlias))
        val endpoint = checkNotNull(AndroidServerConfigurationStore(context).load())
        val cached = checkNotNull(LocalConversationContextStore(context, "default").load(endpoint))
        val friend = cached.friends.single { it.accountId == account }
        NativeProfile(context).use { profile ->
            assertArrayEquals(public, ProfileBridge.publicKey(profile.handle))
            println("HISTORY_PROBE account=$account selected_device=${friend.peerId.take(12)} known_devices=${friend.knownPeerIds.size}")
            val combined=accountHistoryPage(friend,Long.MAX_VALUE){peer,before->
                checkNotNull(ReliableBridge.history(profile.handle,peer.toByteArray(Charsets.US_ASCII),before))}
            println("HISTORY_PROBE combined_messages=${combined.size} label_match=${label != null && combined.any{it.text.trim().equals(label,ignoreCase=true)}}")
            for (peer in friend.knownPeerIds) {
                var before = Long.MAX_VALUE
                var count = 0
                for (page in 0 until 32) {
                    val rows = messageSnapshots(checkNotNull(ReliableBridge.history(profile.handle,
                        peer.toByteArray(Charsets.US_ASCII), before)))
                    count += rows.size
                    for (message in rows) if (label != null && message.text.trim().equals(label, ignoreCase = true)) {
                        println("HISTORY_PROBE label_match=true message_id=${message.id} device=${peer.take(12)} timestamp=${message.timestamp} selected=${peer == friend.peerId}")
                    }
                    if (rows.size < 8) break
                    val next = rows.minOf { it.sequence }
                    check(next > 0 && next < before)
                    before = next
                }
                println("HISTORY_PROBE device=${peer.take(12)} selected=${peer == friend.peerId} messages=$count bounded_pages=32")
            }
        }
        assertArrayEquals(public, security.read("identity.public"))
    }
}
