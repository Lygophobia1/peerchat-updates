package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.platform.AttachmentSendMode
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

/** Explicit opt-in. Uses only the existing authorized codex/codex1 test profiles. */
@RunWith(AndroidJUnit4::class)
class LiveCachedPhotoPairTest {
 @Test fun encryptedPhotoPreviewsBeforeDownloadBothDirections():Unit=runBlocking {
  val instrumentation=InstrumentationRegistry.getInstrumentation();val context=instrumentation.targetContext
  val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull();assumeTrue(port!=null)
  check(context.packageName=="com.peerchat.android.voiceintegration")
  check(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk"))
  context.getSystemService(android.app.job.JobScheduler::class.java).cancelAll()
  val input=Socket("127.0.0.1",port!!).use{val stream=DataInputStream(it.getInputStream());val n=stream.readInt();require(n in 1..4096);JSONObject(ByteArray(n).also{stream.readFully(it)}.toString(Charsets.UTF_8))}
  val endpoint=ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin"))
  val a=MessagingBackend(context,"test-live-codex");val b=MessagingBackend(context,"test-live-codex1")
  var stage="login"
  fun evidence(value:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","CACHE_PHOTO_PAIR $value\n")})
  suspend fun await(label:String,timeout:Long=90000,predicate:()->Boolean){stage=label;val start=System.nanoTime();while(!predicate()){check((System.nanoTime()-start)/1_000_000<timeout){"Timed out: $label"};delay(200)};evidence("stage=$label elapsed_ms=${(System.nanoTime()-start)/1_000_000}")}
  try {
   a.signInRemembered(endpoint,"codex",input.getString("firstPassword"),true);input.remove("firstPassword")
   b.signInRemembered(endpoint,"codex1",input.getString("secondPassword"),true);input.remove("secondPassword")
   await("presence"){a.state.value.serverPresenceLive&&b.state.value.serverPresenceLive}
   await("existing_friendship"){a.state.value.conversations.any{it.friend.accountId==b.state.value.accountId}&&b.state.value.conversations.any{it.friend.accountId==a.state.value.accountId}}
   a.setInUse(true,System.nanoTime()+600_000_000_000L,true,false);b.setInUse(true,System.nanoTime()+600_000_000_000L,true,false)
   await("current_friend_devices"){a.state.value.conversations.any{it.friend.accountId==b.state.value.accountId&&it.friend.peerId==b.state.value.localPeerId}&&b.state.value.conversations.any{it.friend.accountId==a.state.value.accountId&&it.friend.peerId==a.state.value.localPeerId}}
   // Fixture-only settling for both current device-key capability registrations.
   // This is not part of production send latency.
   delay(5000)
   suspend fun send(sender:MessagingBackend,recipient:MessagingBackend,label:String){
    val file=cachedPhotoFixture(context,"dm-photo-$label");val digest=cachedPhotoDigest(file)
    try {
     val friend=sender.state.value.conversations.first{it.friend.accountId==recipient.state.value.accountId&&it.friend.peerId==recipient.state.value.localPeerId}.friend
     stage="send_$label";val start=System.nanoTime()
     val id=sender.sendAttachment(friend.peerId,androidx.core.content.FileProvider.getUriForFile(context,context.packageName+".diagnostics",file),AttachmentSendMode.Cached)
     evidence("direction=$label id=$id bytes=${file.length()} offer_ms=${(System.nanoTime()-start)/1_000_000}")
     fun row(owner:MessagingBackend)=owner.state.value.conversations.flatMap{it.attachments}.singleOrNull{it.id==id}
     await("early_photo_$label",180000){row(recipient)?.awaitingAcceptance==true}
     val source=checkNotNull(row(sender));val incoming=checkNotNull(row(recipient))
     check(source.thumbnail.isNotEmpty()&&incoming.thumbnail==source.thumbnail){"Encrypted preview missing before original acceptance"}
     check(!incoming.complete&&incoming.done==0L&&incoming.localPath.isEmpty())
     check(com.peerchat.android.ui.attachmentThumbnailPicture(incoming.thumbnail)!=null){"Preview could not be rendered"}
     evidence("direction=$label thumbnail_before_accept=true preview_bytes=${java.util.Base64.getDecoder().decode(incoming.thumbnail).size} elapsed_ms=${(System.nanoTime()-start)/1_000_000}")
     recipient.attachmentAction(id,true)
     await("download_$label",240000){val target=row(recipient);check(target?.state !in setOf(5,6,8)){"Photo transfer terminated"};target?.complete==true&&target.localPath.isNotEmpty()}
     check(cachedPhotoDigest(File(checkNotNull(row(recipient)).localPath)).contentEquals(digest)){"Downloaded original digest mismatch"}
     evidence("direction=$label original_sha256_matches=true plaintext_verified=true total_ms=${(System.nanoTime()-start)/1_000_000}")
    } finally {file.delete()}
   }
   send(a,b,"codex_to_codex1");send(b,a,"codex1_to_codex")
  } catch(error:Throwable){evidence("failed_stage=$stage type=${error.javaClass.simpleName}");throw error}
  finally{input.remove("firstPassword");input.remove("secondPassword");a.close();b.close()}
 }
}
