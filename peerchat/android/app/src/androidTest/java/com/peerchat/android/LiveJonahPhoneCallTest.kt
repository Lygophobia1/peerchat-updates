package com.peerchat.android

import android.Manifest
import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.AndroidMessagingRuntime
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

/** Explicit human-authorized, one-call probe. No trust overrides or messages. */
@RunWith(AndroidJUnit4::class)
class LiveJonahPhoneCallTest {
    @Test fun singleAuthorizedCall()=runBlocking {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull()
        assumeTrue("Explicit live call opt-in required",port!=null)
        check(context.packageName=="com.peerchat.android.backgroundintegration")
        check(android.os.Build.MODEL.contains("sdk"))
        val config=Socket().use{socket->
            socket.connect(InetSocketAddress("127.0.0.1",port!!),5000);socket.soTimeout=5000
            val input=DataInputStream(socket.getInputStream());val size=input.readInt();require(size in 1..4096)
            JSONObject(ByteArray(size).also{input.readFully(it)}.toString(Charsets.UTF_8))
        }
        val account=config.getString("account");require(account in setOf("codex","codex1"))
        context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
        val backend=AndroidMessagingRuntime.acquire(context)
        var token:String?=null;var activity:android.app.Activity?=null;var audio:AndroidCallAudio?=null
        var stage="login"
        fun evidence(value:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","PHONE_CALL $value\n")})
        suspend fun await(label:String,predicate:()->Boolean){stage=label;val began=System.nanoTime()
            while(!predicate()){check((System.nanoTime()-began)/1_000_000<60000){"Stage deadline"};delay(100)}
        }
        try {
            backend.signInRemembered(ServerEndpoint(config.getString("host"),config.getInt("port"),config.getString("pin")),account,config.getString("password"),true)
            config.remove("password")
            await("friend_authorized"){backend.state.value.serverPresenceLive&&backend.state.value.conversations.any{it.friend.username.equals("JonahPhone",true)}}
            val friend=backend.state.value.conversations.single{it.friend.username.equals("JonahPhone",true)}.friend
            evidence("target_found=true online=${friend.online}")
            instrumentation.uiAutomation.grantRuntimePermission(context.packageName,Manifest.permission.RECORD_AUDIO)
            activity=instrumentation.startActivitySync(Intent(context,MainActivity::class.java).putExtra("peerchat_ui_demo",true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            audio=AndroidCallAudio(context){evidence("audio_device_failure")}
            check(audio!!.prepare(AndroidCallAudio.Processing()))
            stage="prepare";val began=System.nanoTime()
            val consent=backend.prepareVoice(friend.peerId,null);token=consent.token
            stage="activate";check(backend.activateVoice(consent.token).active)
            evidence("offer_sent=true preparation_ms=${(System.nanoTime()-began)/1_000_000}")
            await("answer_or_end"){backend.state.value.voice.mediaState==2L||!backend.state.value.voice.active}
            val voice=backend.state.value.voice
            evidence("connected=${voice.mediaState==2L} active=${voice.active} control=${voice.controlAvailable}")
            if(voice.mediaState==2L){
                val until=System.nanoTime()+20_000_000_000L
                while(backend.state.value.voice.active&&System.nanoTime()<until)delay(200)
                backend.state.value.voice.mediaDiagnostics.lineSequence().filter{it.startsWith("received_packets=")||it.startsWith("send_api_accepted=")}.forEach{evidence(it)}
            }
            check(voice.mediaState==2L){"Call did not connect"}
        }catch(failure:Throwable){
            val voice=backend.state.value.voice
            evidence("failed_stage=$stage type=${failure.javaClass.simpleName} active=${voice.active} state=${voice.state} media=${voice.mediaState} control=${voice.controlAvailable} verification_pending=${backend.state.value.deviceVerifications.size}")
            throw AssertionError("Phone call failed at $stage (${failure.javaClass.simpleName})")
        }finally{
            config.remove("password")
            if(backend.state.value.voice.active)backend.state.value.voice.target?.let{runCatching{backend.controlVoice(it,VoiceControl.End,false)}}
            token?.let{runCatching{backend.releaseVoice(it)}};audio?.close()
            activity?.let{instrumentation.runOnMainSync{it.finish()}};backend.close()
        }
    }
}
