package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.application.DeliveryTargetStatus
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.net.InetSocketAddress
import java.net.Socket

/** Real Windows counterpart; verifies only the independently supplied test-device
 * fingerprint through the existing explicit verification command. Never accepts
 * an unknown sibling or equates one device's ACK with account-wide completion. */
@RunWith(AndroidJUnit4::class)
class LiveWindowsDmTest {
    @Test fun verifiedWindowsMessageRoundTrip() = runBlocking {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull()
        assumeTrue("Explicit isolated live probe required",port!=null)
        check(context.packageName=="com.peerchat.android.voiceintegration")
        check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
        val input=Socket().use {socket->
            socket.connect(InetSocketAddress("127.0.0.1",port!!),5000);socket.soTimeout=5000
            val stream=DataInputStream(socket.getInputStream());val size=stream.readInt();require(size in 1..4096)
            JSONObject(ByteArray(size).also{stream.readFully(it)}.toString(Charsets.UTF_8))
        }
        input.remove("firstPassword")
        require(input.getString("second")=="codex1")
        val probe=input.getString("probeId");require(probe.matches(Regex("[0-9a-f]{32}")))
        val expected=input.getString("windowsPeer");require(expected.matches(Regex("[0-9a-f]{64}")))
        val endpoint=ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin"))
        val backend=MessagingBackend(context,"test-live-codex1")
        var stage="login"
        fun evidence(text:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream",text+"\n")})
        fun conversation()=backend.state.value.conversations.single{it.friend.username=="codex"}
        suspend fun await(label:String,predicate:()->Boolean) {
            stage=label;val start=System.nanoTime()
            while(!predicate()) {
                check((System.nanoTime()-start)/1_000_000<120000){"stage_timeout"}
                val friend=backend.state.value.conversations.singleOrNull{it.friend.username=="codex"}?.friend
                if(friend!=null&&expected in friend.knownPeerIds&&expected !in friend.revokedPeerIds) {
                    backend.state.value.deviceVerifications.singleOrNull{
                        it.account==friend.accountId&&it.localAccount==backend.state.value.accountId&&it.presented==expected&&it.live
                    }?.let{backend.verifyDevice(it,true);evidence("LIVE_DM independently_compared_test_device=true")}
                }
                delay(100)
            }
            evidence("LIVE_DM_STAGE $label elapsed_ms=${(System.nanoTime()-start)/1_000_000}")
        }
        try {
            backend.signInRemembered(endpoint,"codex1",input.getString("secondPassword"),true);input.remove("secondPassword")
            await("authenticated_friend"){backend.state.value.authenticated&&backend.state.value.conversations.any{it.friend.username=="codex"&&expected in it.friend.knownPeerIds&&expected !in it.friend.revokedPeerIds}}
            val incoming="PC-LIVE-DM-v1:$probe:win-to-android"
            await("windows_durable_receive"){conversation().messages.any{it.senderDevice==expected&&it.text==incoming}}
            val received=conversation().messages.single{it.senderDevice==expected&&it.text==incoming}.id
            evidence("LIVE_DM received_id=$received")
            val reply=backend.send(expected,"PC-LIVE-DM-v1:$probe:android-to-win")
            evidence("LIVE_DM reply_id=$reply")
            await("windows_device_ack"){conversation().messages.any{it.id==reply&&it.deliveryTargets?.targets?.any{t->t.deviceId==expected&&t.status==DeliveryTargetStatus.Acknowledged}==true}}
            val targets=conversation().messages.single{it.id==reply}.deliveryTargets!!
            evidence("LIVE_DM_ACK selection_unresolved=${targets.selectionUnresolved} acknowledged=${targets.targets.count{it.status==DeliveryTargetStatus.Acknowledged}} pending=${targets.targets.count{it.status==DeliveryTargetStatus.Pending}} unavailable=${targets.targets.count{it.status==DeliveryTargetStatus.Unavailable}}")
            backend.setInUse(false);check(backend.awaitQuiescence(30000));backend.setInUse(true)
            check(backend.restoreRemembered(endpoint))
            await("history_after_resume"){backend.state.value.conversations.any{it.friend.username=="codex"&&it.messages.any{m->m.id==received}&&it.messages.any{m->m.id==reply}}}
            evidence("LIVE_DM_RESULT Windows receipt, selected-device ACK and retained history passed; not account-wide completion")
        } catch(failure:Throwable) {
            backend.state.value.conversations.filter{it.friend.username=="codex"}.forEach { row->
                evidence("LIVE_DM_SCOPE selected_matches_expected=${row.friend.peerId==expected} expected_known=${expected in row.friend.knownPeerIds} route_ready=${row.connection.ready}")
                row.messages.filter{it.text=="PC-LIVE-DM-v1:$probe:android-to-win"}.forEach { message->
                    val targets=message.deliveryTargets
                    evidence("LIVE_DM_PENDING id=${message.id} recipient_matches_expected=${message.recipientDevice==expected} selection_unresolved=${targets?.selectionUnresolved} expected_target_status=${targets?.targets?.singleOrNull{it.deviceId==expected}?.status} target_count=${targets?.targets?.size}")
                }
            }
            evidence("LIVE_DM_FAILURE stage=$stage type=${failure.javaClass.simpleName} authenticated=${backend.state.value.authenticated} verification_pending=${backend.state.value.deviceVerifications.size}")
            throw AssertionError("Windows DM probe failed at $stage (${failure.javaClass.simpleName})")
        } finally {input.remove("secondPassword");backend.close()}
    }
}
