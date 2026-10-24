package com.peerchat.android

import android.Manifest
import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.application.VoiceControl
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.audio.AndroidCallAudio
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.net.InetSocketAddress
import java.net.Socket

/** Opt-in live calls between Windows and an isolated emulator. Retains existing
 * test-account identities, uses normal trust checks, never accesses personal profiles. */
@RunWith(AndroidJUnit4::class)
class LiveWindowsCallTest {
    @Test fun windowsCallsBothDirectionsWithoutSelectingConversation() = runBlocking {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val args=InstrumentationRegistry.getArguments()
        val port=args.getString("disposableProbePort")?.toIntOrNull()
        assumeTrue("Explicit Windows/emulator opt-in required",port!=null)
        check(context.packageName in setOf("com.peerchat.android.backgroundintegration","com.peerchat.android.voiceintegration"))
        check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
        // This package is an isolated fixture. Retained periodic wake jobs must
        // not open its unrelated default profile alongside the explicit owner.
        context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
        val channel=Socket()
        val input=channel.let {socket->
            socket.connect(InetSocketAddress("127.0.0.1",port!!),5000);socket.soTimeout=180000
            val stream=DataInputStream(socket.getInputStream());val size=stream.readInt();require(size in 1..4096)
            JSONObject(ByteArray(size).also{stream.readFully(it)}.toString(Charsets.UTF_8))
        }
        val name=input.getString("account");require(name=="codex1")
        val other=if(name=="codex")"codex1" else "codex"
        val backend=MessagingBackend(context,"test-live-$name")
        var stage="login";var token:String?=null;var activity:android.app.Activity?=null
        var audio:AndroidCallAudio?=null
        var expectedPeer=""
        fun evidence(value:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","CALL_PROBE $value\n")})
        suspend fun await(label:String,predicate:()->Boolean){stage=label;val start=System.nanoTime()
            while(!predicate()){
                check((System.nanoTime()-start)/1_000_000<90000){"Bounded stage timeout"}
                val friend=backend.state.value.conversations.singleOrNull{it.friend.username==other}?.friend
                if(friend!=null&&expectedPeer in friend.knownPeerIds&&expectedPeer !in friend.revokedPeerIds){
                    backend.state.value.deviceVerifications.singleOrNull{
                        it.account==friend.accountId&&it.localAccount==backend.state.value.accountId&&it.presented==expectedPeer&&it.live
                    }?.let{backend.verifyDevice(it,true);evidence("independently_compared_test_device=true")}
                }
                delay(50)
            }
        }
        fun count(key:String)=backend.state.value.voice.mediaDiagnostics.lineSequence()
            .firstOrNull{it.startsWith("$key=")}?.substringAfter('=')?.toLongOrNull()?:0L
        try {
            backend.signInRemembered(ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin")),name,input.getString("password"),true)
            input.remove("password")
            val local=backend.state.value.localPeerId
            require(local.matches(Regex("[0-9a-f]{64}")))
            java.io.DataOutputStream(channel.getOutputStream()).apply{write(local.toByteArray(Charsets.US_ASCII));flush()}
            expectedPeer=ByteArray(64).also{DataInputStream(channel.getInputStream()).readFully(it)}.toString(Charsets.US_ASCII)
            require(expectedPeer.matches(Regex("[0-9a-f]{64}")));channel.close()
            // Match the current selectable peer, not merely a historical device
            // learned before the other emulator finished signing in.
            await("friend_authorized"){backend.state.value.serverPresenceLive&&backend.state.value.conversations.any{it.friend.username==other&&it.friend.peerId==expectedPeer}}
            instrumentation.uiAutomation.grantRuntimePermission(context.packageName,Manifest.permission.RECORD_AUDIO)
            // Foreground audio eligibility without a second remembered-account
            // presenter signing in alongside this independently owned test backend.
            activity=instrumentation.startActivitySync(Intent(context,MainActivity::class.java).putExtra("peerchat_ui_demo",true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            audio=AndroidCallAudio(context){evidence("audio_device_failure")}
            check(audio!!.prepare(AndroidCallAudio.Processing()))
            // No selected conversation or Send is needed for any of these calls.
            repeat(4){index->
                val outgoing=index%2==1
                val consent=if(outgoing){
                    await("peer_online"){backend.state.value.conversations.any{it.friend.username==other&&it.friend.online}}
                    stage="prepare_outgoing"
                    backend.prepareVoice(expectedPeer,null)
                }else{
                    await("incoming"){backend.state.value.voice.active&&backend.state.value.voice.state==2L&&backend.state.value.voice.direction==1L}
                    val target=checkNotNull(backend.state.value.voice.target);check(target.peer==expectedPeer)
                    stage="prepare_answer";backend.prepareVoice(target.peer,target)
                }
                token=consent.token;stage="activate"
                val start=System.nanoTime();val active=backend.activateVoice(consent.token)
                check(active.active)
                evidence("iteration=$index outgoing=$outgoing activation_ms=${(System.nanoTime()-start)/1_000_000}")
                await("connected"){backend.state.value.voice.mediaState==2L}
                await("bidirectional_packets"){count("received_packets")>=10&&count("send_api_accepted")>=10}
                check(backend.state.value.voice.controlAvailable)
                evidence("iteration=$index connected=true control=true sent=${count("send_api_accepted")} received=${count("received_packets")}")
                if(outgoing){stage="explicit_end";delay(3000);backend.controlVoice(checkNotNull(backend.state.value.voice.target),VoiceControl.End,false)}
                await("ended"){!backend.state.value.voice.active}
                stage="release";backend.releaseVoice(consent.token);token=null
                evidence("iteration=$index released=true")
            }
        }catch(failure:Throwable){
            val voice=backend.state.value.voice
            evidence("failed_stage=$stage type=${failure.javaClass.simpleName} active=${voice.active} state=${voice.state} media=${voice.mediaState} control=${voice.controlAvailable} sent=${count("send_api_accepted")} received=${count("received_packets")}")
            throw AssertionError("Call probe failed at $stage (${failure.javaClass.simpleName})")
        }finally{
            channel.close();input.remove("password");token?.let{runCatching{backend.releaseVoice(it)}}
            audio?.close();activity?.let{instrumentation.runOnMainSync{it.finish()}};backend.close()
        }
    }
}
