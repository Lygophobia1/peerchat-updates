package com.peerchat.android

import android.Manifest
import android.content.Intent
import android.database.sqlite.SQLiteDatabase
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.application.VoiceControl
import com.peerchat.android.application.DeliveryTargetStatus
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.ProfileSecurity
import com.peerchat.android.platform.audio.AndroidCallAudio
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.net.InetSocketAddress
import java.net.Socket

/** Opt-in retained test-account benchmark. No profile resets, secrets, content or paths in evidence. */
@RunWith(AndroidJUnit4::class)
class LiveWindowsBenchmarkTest {
 @Test fun retainedHistoryBenchmark()=runBlocking {
  val ins=InstrumentationRegistry.getInstrumentation();val context=ins.targetContext;val args=InstrumentationRegistry.getArguments()
  val port=args.getString("disposableProbePort")?.toIntOrNull();assumeTrue("Explicit Windows benchmark opt-in required",port!=null)
  check(context.packageName in setOf("com.peerchat.android.voiceintegration","com.peerchat.android.backgroundintegration"))
  check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
  context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
  val socket=Socket();socket.connect(InetSocketAddress("127.0.0.1",port!!),5000);socket.soTimeout=120000
  val stream=DataInputStream(socket.getInputStream());val output=DataOutputStream(socket.getOutputStream())
  val size=stream.readInt();require(size in 1..4096);val cfg=JSONObject(ByteArray(size).also{stream.readFully(it)}.toString(Charsets.UTF_8))
  require(cfg.getString("account")=="codex1");val mode=cfg.getString("mode");require(mode in setOf("call","dm","seed"))
  val suffix=cfg.optString("profileSuffix");require(suffix in setOf("","-upload224"));val profileName="test-live-codex1$suffix"
  val probe=cfg.getString("probeId");require(probe.matches(Regex("[0-9a-f]{32}")))
  val backend=MessagingBackend(context,profileName);var expected="";var round=-1;var stage="login";var token:String?=null
  var audio:AndroidCallAudio?=null;var activity:android.app.Activity?=null
  fun evidence(text:String)=ins.sendStatus(2,android.os.Bundle().apply{putString("stream",text+"\n")})
  fun mark(phase:String,id:String="")=evidence("BENCH "+JSONObject().put("side","android").put("phase",phase).put("round",round).put("utc_ms",System.currentTimeMillis()).put("mono_ns",System.nanoTime()).put("id",id).put("warmup",round<(if(mode=="call")6 else 10)))
  fun history(label:String){
   val id=ProfileSecurity(context,profileName).id.joinToString(""){"%02x".format(it)}
   val file=File(context.noBackupFilesDir,"profiles/$id/peerchat.sqlite")
   val counts=JSONObject().put("side","android").put("stage",label).put("call_records","not_implemented")
   SQLiteDatabase.openDatabase(file.absolutePath,null,SQLiteDatabase.OPEN_READONLY or SQLiteDatabase.NO_LOCALIZED_COLLATORS).use {db->
    for((name,sql) in listOf("messages" to "SELECT count(*) FROM messages","group_messages" to "SELECT count(*) FROM group_message_index","attachment_index" to "SELECT count(*) FROM attachment_history_index","dm_cached_jobs" to "SELECT count(*) FROM objects WHERE k LIKE 'dm-attachment-job/v1/%'")){
     db.rawQuery(sql,null).use{it.moveToFirst();counts.put(name,it.getLong(0))}
    }
   }
   counts.put("group_cached_projected",backend.state.value.groups.sumOf{it.attachments.size})
   evidence("BENCH_HISTORY $counts")
  }
  suspend fun await(label:String,predicate:()->Boolean){stage=label;val start=System.nanoTime();while(!predicate()){
   check(System.nanoTime()-start<120_000_000_000L){"Stage timeout"}
   val friend=backend.state.value.conversations.singleOrNull{it.friend.username=="codex"}?.friend
   if(friend!=null&&expected in friend.knownPeerIds&&expected !in friend.revokedPeerIds){backend.state.value.deviceVerifications.singleOrNull{
    it.account==friend.accountId&&it.localAccount==backend.state.value.accountId&&it.presented==expected&&it.live
   }?.let{backend.verifyDevice(it,true)}}
   delay(20)
  }}
  fun conversation()=backend.state.value.conversations.single{it.friend.username=="codex"}
  fun packets(key:String)=backend.state.value.voice.mediaDiagnostics.lineSequence().firstOrNull{it.startsWith("$key=")}?.substringAfter('=')?.toLongOrNull()?:0L
  try{
   backend.signInRemembered(ServerEndpoint(cfg.getString("host"),cfg.getInt("port"),cfg.getString("pin")),"codex1",cfg.getString("password"),true);cfg.remove("password")
   val local=backend.state.value.localPeerId;require(local.matches(Regex("[0-9a-f]{64}")));output.write(local.toByteArray(Charsets.US_ASCII));output.flush()
   // Five bounded NTP-style samples; host reports offset and minimum-RTT uncertainty.
   repeat(5){stream.readLong();output.writeLong(System.currentTimeMillis());output.flush()}
   expected=ByteArray(64).also{stream.readFully(it)}.toString(Charsets.US_ASCII);require(expected.matches(Regex("[0-9a-f]{64}")));socket.close()
   await("authenticated_friend"){backend.state.value.serverPresenceLive&&backend.state.value.conversations.any{it.friend.username=="codex"&&it.friend.peerId==expected}}
   history("before")
   if(mode=="call"){
    ins.uiAutomation.grantRuntimePermission(context.packageName,Manifest.permission.RECORD_AUDIO)
    activity=ins.startActivitySync(Intent(context,MainActivity::class.java).putExtra("peerchat_ui_demo",true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    audio=AndroidCallAudio(context){};check(audio!!.prepare(AndroidCallAudio.Processing()))
    repeat(12){index->round=index;val outgoing=index%2==1
     val consent=if(outgoing){await("peer_online"){conversation().friend.online};mark("start");backend.prepareVoice(expected,null)}
      else {await("ring"){backend.state.value.voice.active&&backend.state.value.voice.state==2L&&backend.state.value.voice.direction==1L};val target=checkNotNull(backend.state.value.voice.target);check(target.peer==expected);mark("ring",target.id);mark("accept",target.id);backend.prepareVoice(expected,target)}
     token=consent.token;mark("prepared");val active=backend.activateVoice(consent.token);check(active.active);mark("submitted")
     await("connected"){backend.state.value.voice.mediaState==2L};mark("connected")
     await("received_packet"){packets("received_packets")>0};mark("first_received_packet")
     await("bidirectional_packets"){packets("received_packets")>=10&&packets("send_api_accepted")>=10};mark("bidirectional_packets");check(backend.state.value.voice.controlAvailable)
     if(outgoing){delay(3000);backend.controlVoice(checkNotNull(backend.state.value.voice.target),VoiceControl.End,false)}
     await("ended"){!backend.state.value.voice.active};backend.releaseVoice(consent.token);token=null;mark("ended")
    }
   }else if(mode=="seed"){
    repeat(3){index->round=index
     val name="benchmark-seed-$probe-$index.bin"
     await("cached_seed_offer"){conversation().attachments.any{!it.outgoing&&it.peer==expected&&it.name==name&&it.awaitingAcceptance}}
     val row=conversation().attachments.single{!it.outgoing&&it.peer==expected&&it.name==name};backend.attachmentAction(row.id,true)
     await("cached_seed_verified"){conversation().attachments.any{it.id==row.id&&it.complete}}
     mark("cached_seed_verified",row.id)
    };delay(5000)
   }else{
    repeat(16){index->round=index
     val incoming="PC-BENCH-DM-v1:$probe:windows:$index"
     await("recipient_projection"){conversation().messages.any{it.senderDevice==expected&&it.text==incoming}}
     val received=conversation().messages.single{it.senderDevice==expected&&it.text==incoming};mark("recipient_projection",received.id)
     if(index==10)history("measurement_start")
     mark("send");val sent=backend.sendToAccount(backend.state.value.accountId,conversation().friend.accountId,"PC-BENCH-DM-v1:$probe:android:$index");mark("local_submit",sent)
     await("sender_ack"){conversation().messages.any{it.id==sent&&it.deliveryTargets?.targets?.any{t->t.deviceId==expected&&t.status==DeliveryTargetStatus.Acknowledged}==true}};mark("sender_ack",sent)
    }
    delay(3000)
   }
   history("after")
  }catch(failure:Throwable){evidence("BENCH_FAILURE mode=$mode stage=$stage type=${failure.javaClass.simpleName}");throw AssertionError("Benchmark failed at $stage (${failure.javaClass.simpleName})")}
  finally{socket.close();cfg.remove("password");token?.let{runCatching{backend.releaseVoice(it)}};audio?.close();activity?.let{ins.runOnMainSync{it.finish()}};backend.close()}
 }
}
