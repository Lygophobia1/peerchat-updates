package com.peerchat.android

import android.Manifest
import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
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

/** Real Windows-created room + Android application join. Media is separately
 * opt-in and checks real packet counters, never claims audible quality. */
@RunWith(AndroidJUnit4::class)
class LiveWindowsRoomJoinTest {
    @Test fun joinAuthenticatedWindowsRoom() = runBlocking {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull()
        assumeTrue("Explicit live test opt-in required",port!=null)
        check(context.packageName=="com.peerchat.android.voiceintegration")
        check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
        val input=Socket().use {s->s.connect(InetSocketAddress("127.0.0.1",port!!),5000);s.soTimeout=5000
            val stream=DataInputStream(s.getInputStream());val size=stream.readInt();require(size in 1..4096)
            JSONObject(ByteArray(size).also{stream.readFully(it)}.toString(Charsets.UTF_8))}
        input.remove("firstPassword")
        require(input.getString("second")=="codex1")
        val group=input.getString("groupId");require(group.matches(Regex("[0-9a-f]{32}")))
        val endpoint=ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin"))
        val backend=MessagingBackend(context,"test-live-codex1")
        var stage="login";var token:String?=null
        var activity:android.app.Activity?=null;var audio:AndroidCallAudio?=null
        fun evidence(text:String){instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream",text+"\n")})}
        suspend fun await(label:String,predicate:()->Boolean){stage=label;val start=System.nanoTime()
            while(!predicate()){check((System.nanoTime()-start)/1_000_000<90000){"stage_timeout"};delay(100)}
            evidence("LIVE_WINDOWS_STAGE $label elapsed_ms=${(System.nanoTime()-start)/1_000_000}")}
        try {
            backend.signInRemembered(endpoint,"codex1",input.getString("secondPassword"),true);input.remove("secondPassword")
            await("group_visible"){backend.state.value.groups.any{it.id==group}||backend.state.value.groupInvitations.any{it.group==group}}
            backend.state.value.groupInvitations.singleOrNull{it.group==group}?.let{backend.respondGroup(it.id,true)}
            await("group_authorized"){backend.state.value.groups.any{it.id==group&&it.ready}}
            await("room_visible"){backend.state.value.voiceRooms.any{it.group==group&&it.rooms.isNotEmpty()}}
            instrumentation.uiAutomation.grantRuntimePermission(context.packageName,Manifest.permission.RECORD_AUDIO)
            activity=instrumentation.startActivitySync(Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            instrumentation.waitForIdleSync()
            audio=AndroidCallAudio(context){}
            check(audio.prepare())
            val room=backend.state.value.voiceRooms.single{it.group==group}.rooms.first()
            stage="prepare_group_voice"
            val started=System.nanoTime();val consent=backend.prepareGroupVoice(group,room.id);token=consent.token
            stage="activate_group_voice"
            val voice=backend.activateVoice(consent.token)
            check(voice.active&&voice.group==group&&voice.room==room.id)
            evidence("LIVE_WINDOWS_STAGE group_join elapsed_ms=${(System.nanoTime()-started)/1_000_000}")
            await("both_room_participants"){backend.state.value.voiceRooms.any{it.group==group&&it.rooms.any{r->r.id==room.id&&r.members.size>=2}}}
            if(input.optBoolean("media")) {
                fun count(name:String)=backend.state.value.voice.mediaDiagnostics.lineSequence()
                    .firstOrNull{it.startsWith("$name=")}?.substringAfter('=')?.toLongOrNull()?:0L
                await("encrypted_media_received"){count("group_connected_routes")>0&&count("group_received_packets")>=10&&count("group_encoded_packets")>=10}
                delay(5000)
                evidence("LIVE_WINDOWS_MEDIA connected=${count("group_connected_routes")} encoded=${count("group_encoded_packets")} received=${count("group_received_packets")} mixed=${count("group_mixed_10ms_blocks")}")
            } else delay(2000)
            check(backend.state.value.voice.active)
            evidence(if(input.optBoolean("media"))"LIVE_WINDOWS_RESULT authenticated group media counters passed; audible quality not assessed" else "LIVE_WINDOWS_RESULT authenticated room join and local audio lifecycle passed; remote media not established by signaling probe")
        } catch(failure:Throwable){
            evidence("LIVE_WINDOWS_FAILURE stage=$stage type=${failure.javaClass.simpleName} authenticated=${backend.state.value.authenticated} groups=${backend.state.value.groups.size} rooms=${backend.state.value.voiceRooms.size}")
            throw AssertionError("Live Windows room test failed at $stage (${failure.javaClass.simpleName})")
        } finally {
            input.remove("secondPassword")
            token?.let{runCatching{backend.releaseVoice(it)}}
            audio?.close();activity?.let{instrumentation.runOnMainSync{it.finish()}}
            backend.close()
        }
    }
}
