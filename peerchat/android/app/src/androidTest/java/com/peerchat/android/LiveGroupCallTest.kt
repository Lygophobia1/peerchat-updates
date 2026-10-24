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
import java.net.Socket

/** Opt-in two-account group interoperability. Normal signed membership and media only. */
@RunWith(AndroidJUnit4::class)
class LiveGroupCallTest {
 @Test fun joinBothOrders():Unit=runBlocking {
  val instrumentation=InstrumentationRegistry.getInstrumentation();val context=instrumentation.targetContext
  val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull();assumeTrue(port!=null)
  check(context.packageName=="com.peerchat.android.backgroundintegration")
  check(android.os.Build.MODEL.contains("sdk"))
  context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
  val channel=Socket("127.0.0.1",port!!);channel.soTimeout=240000
  val input=DataInputStream(channel.getInputStream());val size=input.readInt();require(size in 1..4096)
  val config=JSONObject(ByteArray(size).also{input.readFully(it)}.toString(Charsets.UTF_8))
  val account=config.getString("account");require(account in setOf("codex","codex1"))
  val group=config.getString("group");require(group.matches(Regex("[0-9a-f]{32}")))
  val roomName=config.getString("roomName");require(roomName.startsWith("Interop"))
  val reader=channel.getInputStream().bufferedReader();val writer=channel.getOutputStream().bufferedWriter()
  fun signal(text:String){writer.write(text+"\n");writer.flush()}
  fun evidence(text:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","GROUP_CALL $text\n")})
  val backend=MessagingBackend(context,"test-live-$account")
  var stage="login";var token:String?=null;var audio:AndroidCallAudio?=null;var activity:android.app.Activity?=null
  suspend fun await(label:String,predicate:()->Boolean){stage=label;val start=System.nanoTime();while(!predicate()){check((System.nanoTime()-start)/1_000_000<120000){"Stage timeout: $label"};delay(100)};evidence("stage=$label elapsed_ms=${(System.nanoTime()-start)/1_000_000}")}
  fun count(key:String)=backend.state.value.voice.mediaDiagnostics.lineSequence().firstOrNull{it.startsWith("$key=")}?.substringAfter('=')?.toLongOrNull()?:0L
  try {
   backend.signInRemembered(ServerEndpoint(config.getString("host"),config.getInt("port"),config.getString("pin")),account,config.getString("password"),true);config.remove("password")
   await("group_or_invite"){backend.state.value.groups.any{it.id==group&&it.ready}||backend.state.value.groupInvitations.any{it.group==group&&it.inviter in setOf("codex","codex1")}}
   backend.state.value.groupInvitations.singleOrNull{it.group==group}?.let{backend.respondGroup(it.id,true)}
   await("authorized_group"){backend.state.value.groups.any{it.id==group&&it.ready}}
   val members=backend.state.value.groups.single{it.id==group}.memberRows.filter{!it.invited}.map{it.account}.toSet()
   check(members.size==2){"Only two test accounts allowed"}
   instrumentation.uiAutomation.grantRuntimePermission(context.packageName,Manifest.permission.RECORD_AUDIO)
   activity=instrumentation.startActivitySync(Intent(context,MainActivity::class.java).putExtra("peerchat_ui_demo",true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
   audio=AndroidCallAudio(context){evidence("audio_device_failure")};check(audio!!.prepare(AndroidCallAudio.Processing()))
   signal("READY")
   repeat(2){round->
    check(reader.readLine()=="JOIN")
    await("room_$round"){backend.state.value.voiceRooms.any{it.group==group&&it.rooms.any{r->r.name==roomName}}}
    val room=backend.state.value.voiceRooms.single{it.group==group}.rooms.single{it.name==roomName}
    val start=System.nanoTime();val consent=backend.prepareGroupVoice(group,room.id);token=consent.token
    val voice=backend.activateVoice(consent.token);check(voice.active&&voice.group==group&&voice.target?.room==room.id)
    evidence("round=$round join_ms=${(System.nanoTime()-start)/1_000_000} control=${voice.controlAvailable}");signal("JOINED")
    await("bidirectional_media_$round"){count("group_connected_routes")>0&&count("group_received_packets")>10&&count("group_encoded_packets")>10}
    check(backend.state.value.voice.controlAvailable&&backend.state.value.voice.mediaState==2L)
    evidence("round=$round connected_ms=${(System.nanoTime()-start)/1_000_000} sent=${count("group_encoded_packets")} received=${count("group_received_packets")} mixed=${count("group_mixed_10ms_blocks")}")
    val target=checkNotNull(backend.state.value.voice.target);backend.controlVoice(target,VoiceControl.Mute,true);check(backend.state.value.voice.muted)
    backend.controlVoice(target,VoiceControl.Mute,false);check(!backend.state.value.voice.muted)
    delay(3000);signal("MEDIA_OK")
    check(reader.readLine()=="LEAVE");backend.controlVoice(target,VoiceControl.End,false);backend.releaseVoice(consent.token);token=null
    await("left_$round"){!backend.state.value.voice.active};signal("LEFT")
   }
  }catch(error:Throwable){evidence("failed_stage=$stage type=${error.javaClass.simpleName}");throw error}
  finally{config.remove("password");backend.state.value.voice.target?.let{runCatching{backend.controlVoice(it,VoiceControl.End,false)}};token?.let{runCatching{backend.releaseVoice(it)}};audio?.close();activity?.let{instrumentation.runOnMainSync{it.finish()}};backend.close();channel.close()}
 }
}
