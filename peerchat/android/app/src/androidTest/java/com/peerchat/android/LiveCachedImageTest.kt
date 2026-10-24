package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.AttachmentSendMode
import kotlinx.coroutines.*
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.net.Socket

/** Explicit opt-in: one generated test image to the user-authorized JonahPhone.
 * Uses the normal explicit cached-send first-friend enrollment; never invokes a verification override. */
@RunWith(AndroidJUnit4::class)
class LiveCachedImageTest {
 @Test fun uploadGeneratedImage():Unit=runBlocking {
  val instrumentation=InstrumentationRegistry.getInstrumentation()
  val context=instrumentation.targetContext
  val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull()
  assumeTrue(port!=null)
  check(context.packageName=="com.peerchat.android.voiceintegration")
  check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
  context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
  val input=Socket("127.0.0.1",port!!).use{socket->
   val stream=DataInputStream(socket.getInputStream());val n=stream.readInt();require(n in 1..4096)
   JSONObject(ByteArray(n).also{stream.readFully(it)}.toString(Charsets.UTF_8))
  }
  val target=InstrumentationRegistry.getArguments().getString("recipient")?:"JonahPhone"
  require(target in setOf("JonahPhone","codex","codex1"))
  val name=input.getString("account");require(name in setOf("codex","codex1"))
  val suffix=InstrumentationRegistry.getArguments().getString("profileSuffix")?:""
  require(suffix.matches(Regex("[a-z0-9-]{0,32}")))
  val backend=MessagingBackend(context,"test-live-$name$suffix")
  fun evidence(s:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","CACHE_IMAGE $s\n")})
  var stage="login"
  var leaseChurn:Job?=null
  val startupLeaseChurn=InstrumentationRegistry.getArguments().getString("startupLeaseChurn")=="true"
  try {
   backend.signInRemembered(ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin")),name,input.getString("password"),true)
   input.remove("password")
   if(startupLeaseChurn){
    require(target in setOf("codex","codex1"))
    leaseChurn=launch(Dispatchers.Default){repeat(100){
     backend.setInUse(true,System.nanoTime()+600_000_000_000L,true,it%2==0)
     delay(200)
    }}
    evidence("startup_active_lease_churn=true")
   }
   val waiting=System.nanoTime()
   stage="authorized_contact"
   while(backend.state.value.conversations.none{it.friend.username.equals(target,true)}) {
    check((System.nanoTime()-waiting)/1_000_000<45000){"JonahPhone is not an existing contact"};delay(200)
   }
   // The startup variant exercises registration during service lease changes.
   if(!startupLeaseChurn)delay(5000)
   else {val began=System.nanoTime();while(!backend.state.value.serverPresenceLive){check(System.nanoTime()-began<30_000_000_000L);delay(100)}}
   check(backend.state.value.authenticated&&backend.state.value.serverPresenceLive)
   val friend=backend.state.value.conversations.single{it.friend.username.equals(target,true)}.friend
   evidence("target=$target existing_contact=true")
   val historical=backend.state.value
   val historyMessages=historical.conversations.sumOf{it.messages.size}
   val historyFiles=historical.conversations.sumOf{it.attachments.size}
   val historyGroupFiles=historical.groups.sumOf{it.attachments.size}
   evidence("history_before_send projected_messages=$historyMessages projected_dm_files=$historyFiles projected_group_files=$historyGroupFiles local_history_ready=${historical.localHistoryReady}")
   if(InstrumentationRegistry.getArguments().getString("requireHistory")=="true") {
    check(historical.localHistoryReady&&historyMessages>0&&historyFiles+historyGroupFiles>0){"Benchmark requires retained message and attachment history"}
   }
   InstrumentationRegistry.getArguments().getString("cleanupTransfer")?.let{id->
    require(id.matches(Regex("[0-9a-f]{32}")))
    delay(5000)
    require(id=="6cc8a720932744e2a68d1c703d2170df") // Only this recorded generated fixture
    backend.attachmentAction(id,false)
    evidence("test_transfer_canceled=$id")
    return@runBlocking
   }

   val file=java.io.File(java.io.File(context.cacheDir,"diagnostics").also{it.mkdirs()},"PeerChat-upload-test.png")
   val edge=InstrumentationRegistry.getArguments().getString("imageEdge")?.toInt()?:768
   require(edge in 128..1024)
   val bitmap=android.graphics.Bitmap.createBitmap(edge,edge,android.graphics.Bitmap.Config.ARGB_8888)
   val random=java.util.Random(214);val pixels=IntArray(edge*edge){0xff000000.toInt() or random.nextInt(0x1000000)}
   bitmap.setPixels(pixels,0,edge,0,0,edge,edge)
   android.graphics.Canvas(bitmap).apply{scale(edge/768f,edge/768f);drawRect(0f,0f,768f,120f,android.graphics.Paint().apply{color=0xff122433.toInt()});drawText("PeerChat cached upload test",24f,76f,android.graphics.Paint().apply{color=android.graphics.Color.WHITE;textSize=38f;isAntiAlias=true})}
   file.outputStream().use{check(bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG,100,it))};bitmap.recycle()
   val batch=InstrumentationRegistry.getArguments().getString("batchCount")?.toInt()?:1
   require(batch in 1..4)
   if(batch>1){
    require(target in setOf("codex","codex1"))
    stage="batch_send";val began=System.nanoTime()
    val ids=mutableListOf<String>();repeat(batch){index->
     val id=backend.sendAttachment(friend.peerId,androidx.core.content.FileProvider.getUriForFile(context,context.packageName+".diagnostics",file),AttachmentSendMode.Cached)
     evidence("batch_index=$index id=$id bytes=${file.length()} offer_ms=${(System.nanoTime()-began)/1_000_000}");ids.add(id)
    }
    check(ids.toSet().size==batch)
    backend.setInUse(true,System.nanoTime()+600_000_000_000L,true,false)
    val completed=mutableSetOf<String>();stage="batch_upload"
    while(completed.size<batch&&(System.nanoTime()-began)/1_000_000<240000){
     val rows=backend.state.value.conversations.flatMap{it.attachments}.filter{it.id in ids}
     for(row in rows){
      check(row.state !in setOf(5,6,8)){"Batch transfer failed: ${row.error}"}
      if(row.cachePhase in setOf("Notice stored; safe to disconnect","Upload complete; storing notices","Recipients verified saved")&&completed.add(row.id))
       evidence("batch_id=${row.id} server_upload_confirmed=true total_ms=${(System.nanoTime()-began)/1_000_000}")
     }
     delay(50)
    }
    check(completed.size==batch){"Batch server upload confirmation timeout"}
    evidence("batch_count=$batch all_uploaded=true total_ms=${(System.nanoTime()-began)/1_000_000}")
    file.delete();return@runBlocking
   }
   val deleteThenResend=InstrumentationRegistry.getArguments().getString("deleteThenResend")=="true"
   if(deleteThenResend)require(target in setOf("codex","codex1"))
   repeat(if(deleteThenResend)2 else 1){round->
   stage="send";val start=System.nanoTime()
   val id=backend.sendAttachment(friend.peerId,androidx.core.content.FileProvider.getUriForFile(context,context.packageName+".diagnostics",file),AttachmentSendMode.Cached)
   evidence("id=$id bytes=${file.length()} offer_ms=${(System.nanoTime()-start)/1_000_000}")
   // Same policy handoff as a retained explicit transfer lease. This fixture
   // validates owner continuity, not Android foreground-service lifecycle.
   backend.setInUse(true,System.nanoTime()+600_000_000_000L,true,false)
   stage="upload";var previous="";var completed=false;var firstProgress=false;var uploadPhase=false
   while((System.nanoTime()-start)/1_000_000<240000) {
    val row=backend.state.value.conversations.flatMap{it.attachments}.singleOrNull{it.id==id}
    if(!uploadPhase&&row?.cachePhase in setOf("Uploading encrypted file","Offer delivered; uploading encrypted file")){uploadPhase=true;evidence("upload_phase_ms=${(System.nanoTime()-start)/1_000_000}")}
    if(!firstProgress&&row!=null&&row.done>0){firstProgress=true;evidence("first_confirmed_bytes_ms=${(System.nanoTime()-start)/1_000_000}")}
    val progress=row?.let{"state=${it.state} done=${it.done} total=${it.size} phase=${it.cachePhase} error=${it.error}"}?:"not_projected"
    if(progress!=previous){evidence("elapsed_ms=${(System.nanoTime()-start)/1_000_000} $progress");previous=progress}
    if(row?.cachePhase in setOf("Notice stored; safe to disconnect","Upload complete; storing notices","Recipients verified saved")){completed=true;break}
    if(row?.state in setOf(5,6,8))error("Transfer stopped")
    delay(50)
   }
   check(completed){"Server upload confirmation timeout"}
   evidence("server_upload_confirmed=true total_ms=${(System.nanoTime()-start)/1_000_000}")
   backend.state.value.transferDiagnostics.lineSequence().filter{it.contains("transfer="+id)||it.startsWith("cache_queue=")}.forEach{evidence("final_diagnostic=$it")}
   if(deleteThenResend&&round==0){
    stage="delete_own_test_photo"
    backend.deleteLocalMessage(backend.state.value.accountId,"account:${friend.accountId}",id,id)
    check(backend.state.value.conversations.flatMap{it.attachments}.none{it.id==id})
    check(backend.state.value.authenticated)
    evidence("new_test_photo_deleted=true next_send_same_session=true")
   }
   }
   file.delete()
  }catch(e:Throwable){
   delay(11000)
   evidence("native_diagnostics=${backend.state.value.transferDiagnostics}")
   evidence("failed_stage=$stage type=${e.javaClass.simpleName} directory=${backend.state.value.identityDirectoryStatus}")
   if(e is com.peerchat.android.application.AttachmentRecipientVerificationRequired){
    e.devices.forEach{evidence("unverified_recipient_device=$it")}
    backend.state.value.deviceVerifications.filter{it.contact.equals(target,true)}.forEach{evidence("verification_presented=${it.presented} live=${it.live}")}
   }
   throw e
  }
  finally{leaseChurn?.cancelAndJoin();input.remove("password");backend.close()}
 }
}
