package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.DeliveryTargetStatus
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in retained disposable profiles only. Uses existing protected authority;
 * never enrolls, resets keys, changes friendships or reports account-wide ACK. */
@RunWith(AndroidJUnit4::class)
class LiveResumeLatencyTest {
    @Test fun durableQueueDuringSavedResumeAndSelectedDeviceReceipt()=runBlocking {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val args=InstrumentationRegistry.getArguments()
        assumeTrue(args.getString("retainedResumeProbe")=="true")
        val context=instrumentation.targetContext
        check(context.packageName=="com.peerchat.android.voiceintegration")
        check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
        val endpoint=ServerEndpoint(checkNotNull(args.getString("probeHost")),7443,checkNotNull(args.getString("probePin")))
        require(endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
        val first=MessagingBackend(context,"test-live-codex")
        val second=MessagingBackend(context,"test-live-codex1")
        var stage="retained_resume"
        fun evidence(value:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream",value+"\n")})
        suspend fun await(label:String,ready:()->Boolean){
            stage=label;val start=System.nanoTime()
            while(!ready()){
                check((System.nanoTime()-start)/1_000_000<60000){"bounded_stage_timeout"}
                delay(10)
            }
        }
        try{
            check(first.restoreRemembered(endpoint)&&second.restoreRemembered(endpoint))
            check(first.state.value.username=="codex"&&second.state.value.username=="codex1")
            val firstPeer=first.state.value.localPeerId;val secondPeer=second.state.value.localPeerId
            await("authenticated_friend_mapping"){
                first.state.value.conversations.any{it.friend.username=="codex1"&&secondPeer in it.friend.knownPeerIds}&&
                second.state.value.conversations.any{it.friend.username=="codex"&&firstPeer in it.friend.knownPeerIds}
            }
            // The exact peer keys come independently from the two owned profiles.
            // Existing verified contacts are retained; no sibling is auto-trusted.
            suspend fun verifyExpected(){
                for((owner,expected) in listOf(first to secondPeer,second to firstPeer)){
                    owner.state.value.deviceVerifications.singleOrNull{it.presented==expected&&it.live&&it.localAccount==owner.state.value.accountId}
                        ?.let{owner.verifyDevice(it,true)}
                }
            }
            for(round in 0..3){
                val sender=if(round%2==0)first else second
                val recipient=if(round%2==0)second else first
                val expected=recipient.state.value.localPeerId
                sender.setInUse(false);check(sender.awaitQuiescence(30000));sender.setInUse(true)
                val resumeStart=System.nanoTime()
                val resume=async(Dispatchers.IO){sender.restoreRemembered(endpoint)}
                await("resume_started"){resume.isCompleted||sender.state.value.status=="Signing in with saved authorization"}
                val overlapped=!resume.isCompleted
                stage="local_durable_queue"
                evidence("RESUME_PROBE_SCOPE round=$round local_history=${sender.state.value.localHistoryReady} canonical_matches=${sender.state.value.conversations.any{it.friend.peerId==expected}} known_matches=${sender.state.value.conversations.any{expected in it.friend.knownPeerIds}}")
                val sendStart=System.nanoTime()
                val id=sender.send(expected,"PeerChat authorized resume latency probe "+java.util.UUID.randomUUID())
                val committed=System.nanoTime()
                evidence("RESUME_PROBE_COMMIT round=$round id=$id overlap=$overlapped elapsed_ms=${(committed-sendStart)/1_000_000}")
                check(resume.await())
                val resumed=System.nanoTime()
                val receiptStart=System.nanoTime()
                while(recipient.state.value.conversations.none{row->row.messages.any{it.id==id}}){
                    stage="durable_receive";check((System.nanoTime()-receiptStart)/1_000_000<60000)
                    verifyExpected();delay(10)
                }
                val received=System.nanoTime()
                evidence("RESUME_PROBE_RECEIVE round=$round id=$id elapsed_ms=${(received-sendStart)/1_000_000}")
                await("authenticated_selected_device_ack"){
                    sender.state.value.conversations.any{row->row.messages.any{message->message.id==id&&(
                        message.deliveryTargets?.targets?.any{target->target.deviceId==expected&&target.status==DeliveryTargetStatus.Acknowledged}==true||
                        // Local queueing before account authorization takes the
                        // existing legacy canonical-device path. Its native ACK
                        // is valid for that exact device, never all account devices.
                        message.deliveryTargets==null&&message.recipientDevice==expected&&message.delivery==2L)}}
                }
                val acknowledged=System.nanoTime()
                val result=sender.state.value.conversations.flatMap{it.messages}.single{it.id==id}
                evidence("RESUME_PROBE_ACK round=$round id=$id kind=${if(result.deliveryTargets==null)"canonical_device_only" else "target_manifest_device"} selection_unresolved=${result.deliveryTargets?.selectionUnresolved?:true}")
                evidence("RESUME_PROBE round=$round id=$id overlap=$overlapped local_commit_ms=${(committed-sendStart)/1_000_000} resume_ms=${(resumed-resumeStart)/1_000_000} receive_from_send_ms=${(received-sendStart)/1_000_000} ack_from_send_ms=${(acknowledged-sendStart)/1_000_000}")
            }
            evidence("RESUME_PROBE_RESULT retained Android owners passed; selected-device ACK only; emulator observations, not physical phone or Windows interoperability")
        }catch(failure:Throwable){
            evidence("RESUME_PROBE_FAILURE stage=$stage type=${failure.javaClass.simpleName} category=${when(failure.message){"Check failed."->"admission_check";"Required value was null."->"native_null";"bounded_stage_timeout"->"timeout";else->"redacted"}}")
            throw AssertionError("Retained resume probe failed at $stage (${failure.javaClass.simpleName})")
        }finally{first.close();second.close()}
    }
}
