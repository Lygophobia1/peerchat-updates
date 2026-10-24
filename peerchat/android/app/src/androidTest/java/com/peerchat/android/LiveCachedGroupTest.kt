package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.io.File
import java.net.Socket

/** Opt-in live group transfer on the two user-authorized test accounts only. */
@RunWith(AndroidJUnit4::class)
class LiveCachedGroupTest {
 @Test fun groupFilesBothDirections():Unit=runBlocking {
  val instrumentation=InstrumentationRegistry.getInstrumentation();val context=instrumentation.targetContext
  val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull();assumeTrue(port!=null)
  check(context.packageName=="com.peerchat.android.voiceintegration")
  check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
  context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
  val input=Socket("127.0.0.1",port!!).use{val stream=DataInputStream(it.getInputStream());val n=stream.readInt();require(n in 1..4096);JSONObject(ByteArray(n).also{stream.readFully(it)}.toString(Charsets.UTF_8))}
  val endpoint=ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin"))
  val a=MessagingBackend(context,"test-live-codex");val b=MessagingBackend(context,"test-live-codex1")
  var stage="login";var group=""
  val photo=InstrumentationRegistry.getArguments().getString("photoFixture")=="true"
  fun evidence(value:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","CACHE_GROUP $value\n")})
  suspend fun await(label:String,timeout:Long=90000,predicate:()->Boolean){stage=label;val start=System.nanoTime();while(!predicate()){check((System.nanoTime()-start)/1_000_000<timeout){"Timed out: $label"};delay(200)};evidence("stage=$label elapsed_ms=${(System.nanoTime()-start)/1_000_000}")}
  try {
   a.signInRemembered(endpoint,"codex",input.getString("firstPassword"),true);input.remove("firstPassword")
   b.signInRemembered(endpoint,"codex1",input.getString("secondPassword"),true);input.remove("secondPassword")
   await("presence"){a.state.value.serverPresenceLive&&b.state.value.serverPresenceLive}
   await("existing_friendship"){a.state.value.conversations.any{it.friend.accountId==b.state.value.accountId}&&b.state.value.conversations.any{it.friend.accountId==a.state.value.accountId}}
   await("authenticated_group_snapshots"){a.state.value.groupDiagnostics.startsWith("Authenticated group snapshots=")&&b.state.value.groupDiagnostics.startsWith("Authenticated group snapshots=")}
   // Reuse a currently authorized fixture, never depend on retired device keys.
   fun existing()=a.state.value.groups.firstOrNull{g->g.name.startsWith("Cache test ")&&g.ready&&b.state.value.groups.any{it.id==g.id&&it.ready}&&g.memberRows.filter{!it.invited}.map{it.account}.toSet()==setOf(a.state.value.accountId,b.state.value.accountId)}
   group=existing()?.id.orEmpty()
   if(group.isEmpty()) {
    val name="Cache test audit "+java.util.UUID.randomUUID().toString().take(8)
    stage="create_test_group";a.createGroup(name)
    await("created_test_group"){a.state.value.groups.any{it.name==name&&it.ready}}
    group=a.state.value.groups.single{it.name==name&&it.ready}.id
    a.inviteGroup(group,b.state.value.accountId)
    await("test_group_invitation"){b.state.value.groupInvitations.any{it.group==group}}
    b.respondGroup(b.state.value.groupInvitations.single{it.group==group}.id,true)
    await("test_group_authorized"){existing()?.id==group}
   }
   evidence("test_group_authorized=true")
   if(photo){a.setInUse(true,System.nanoTime()+600_000_000_000L,true,false);b.setInUse(true,System.nanoTime()+600_000_000_000L,true,false);delay(5000)}
   suspend fun send(sender:MessagingBackend,recipient:MessagingBackend,label:String){
    val content=("PeerChat generated group cache validation $label\n").repeat(4096)
    val file=if(photo)cachedPhotoFixture(context,"group-photo-$label")else File(File(context.cacheDir,"diagnostics").also{it.mkdirs()},"group-test-$label.txt").also{it.writeText(content,Charsets.UTF_8)}
    val originalDigest=cachedPhotoDigest(file)
    val start=System.nanoTime();stage="send_$label"
    val id=sender.sendGroupAttachment(group,androidx.core.content.FileProvider.getUriForFile(context,context.packageName+".diagnostics",file))
    evidence("direction=$label id=$id bytes=${file.length()} local_offer_ms=${(System.nanoTime()-start)/1_000_000}")
    sender.setInUse(true,System.nanoTime()+600_000_000_000L,true,false)
    fun row(owner:MessagingBackend)=owner.state.value.groups.singleOrNull{it.id==group}?.attachments?.singleOrNull{it.id==id}
    await("offer_$label",180000){row(recipient)?.awaitingAcceptance==true}
    evidence("direction=$label recipient_offer_ms=${(System.nanoTime()-start)/1_000_000} sender_phase=${row(sender)?.cachePhase}")
    if(photo){val source=checkNotNull(row(sender));val incoming=checkNotNull(row(recipient));check(source.thumbnail.isNotEmpty()&&incoming.thumbnail==source.thumbnail){"Encrypted group preview missing before acceptance"};check(!incoming.complete&&incoming.done==0L&&incoming.localPath.isEmpty());check(com.peerchat.android.ui.attachmentThumbnailPicture(incoming.thumbnail)!=null);evidence("direction=$label thumbnail_before_accept=true preview_bytes=${java.util.Base64.getDecoder().decode(incoming.thumbnail).size}")}
    recipient.attachmentAction(id,true)
    var last="";await("download_$label",240000){
     val source=row(sender);val target=row(recipient);val status="sender=${source?.state}/${source?.done}/${source?.cachePhase} recipient=${target?.state}/${target?.done}/${target?.cachePhase} error=${source?.error}|${target?.error}"
     if(status!=last){evidence("direction=$label elapsed_ms=${(System.nanoTime()-start)/1_000_000} $status");last=status}
     check(source?.state !in setOf(5,6,8)&&target?.state !in setOf(5,6,8)){"Group transfer terminated"};target?.complete==true&&target.localPath.isNotEmpty()
    }
    check(cachedPhotoDigest(File(checkNotNull(row(recipient)).localPath)).contentEquals(originalDigest)){"Downloaded plaintext digest mismatch"}
    evidence("direction=$label plaintext_matches=true total_ms=${(System.nanoTime()-start)/1_000_000}")
    await("receipt_$label"){row(sender)?.recipients?.any{it.account==recipient.state.value.accountId&&it.state=="verified"}==true||row(sender)?.complete==true}
    row(sender)?.recipients?.forEach{evidence("direction=$label recipient_account=${it.account} state=${it.state} done=${it.done} total=${it.total}")}
    file.delete()
   }
   send(a,b,"codex_to_codex1");send(b,a,"codex1_to_codex")
  }catch(e:Throwable){evidence("failed_stage=$stage type=${e.javaClass.simpleName}");throw e}
  finally{input.remove("firstPassword");input.remove("secondPassword");a.close();b.close()}
 }
}
