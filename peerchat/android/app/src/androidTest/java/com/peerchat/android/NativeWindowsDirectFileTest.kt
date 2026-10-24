package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.NativeRecord
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.application.attachmentSnapshots
import com.peerchat.android.application.relayExecutions
import com.peerchat.android.platform.AndroidRecipientKeyProvider
import com.peerchat.android.platform.PeerIdentityFormat
import com.peerchat.android.platform.ProfileSecurity
import java.io.File
import java.net.Socket
import java.util.UUID
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Real emulator JNI/POSIX/Keystore against Windows Schannel. ADB carries opaque
 * encrypted frames only; no server, UI, physical-network or listening claim. */
@RunWith(AndroidJUnit4::class)
class NativeWindowsDirectFileTest {
 @Test(timeout=180_000) fun bidirectionalDirectFiles() {
  val context=InstrumentationRegistry.getInstrumentation().targetContext
  val port=InstrumentationRegistry.getArguments().getString("bridgePort")?.toIntOrNull()
  assumeTrue("Requires isolated bridge",port!=null)
  require(context.packageName.endsWith("integration")&&(android.os.Build.FINGERPRINT.contains("generic")||android.os.Build.MODEL.contains("sdk")))
  require(port!! in 1024..65535)
  val name="test_direct_${UUID.randomUUID().toString().replace("-","")}"
  val security=ProfileSecurity(context,name)
  fun record(vararg fields:String)=NativeRecordWriter(30).also{w->fields.forEach(w::text)}.bytes()
  fun content(android:Boolean)=ByteArray(2*1024*1024+123){((it*31+if(android)7 else 11)%251).toByte()}
  try { NativeProfile(context,name).use { profile ->
   val h=profile.handle
   Socket("127.0.0.1",port).use { socket ->
    socket.soTimeout=45000
    val input=socket.getInputStream().bufferedReader(Charsets.US_ASCII)
    val output=socket.getOutputStream().writer(Charsets.US_ASCII).buffered()
    fun line(value:String){require(value.length<=131080);output.write(value);output.newLine();output.flush()}
    fun read():String=checkNotNull(input.readLine()).also{require(it.length<=131080)}
    fun hex(bytes:ByteArray):String {val digits="0123456789abcdef";return CharArray(bytes.size*2){i->val b=bytes[i/2].toInt() and 255;digits[if(i%2==0)b ushr 4 else b and 15]}.concatToString()}
    fun bytes(text:String):ByteArray {require(text.length%2==0&&text.length<=131072);return ByteArray(text.length/2){text.substring(it*2,it*2+2).toInt(16).toByte()}}
    line(PeerIdentityFormat.peerId(checkNotNull(ProfileBridge.publicKey(h))))
    val remote=read();require(remote.matches(Regex("[0-9a-f]{64}")))
    val friends=NativeRecordWriter(1).apply{number(1);number(20);text(remote);number(1);text(remote)}.bytes()
    assertEquals(0,ReliableBridge.friends(h,10,1,friends))
    assertEquals(0,MailboxBridge.configureEnrollment(h,10,1,"fixture.local:7443".toByteArray(),"f".repeat(64).toByteArray()))
    assertEquals(0,ReliableBridge.presence(h,remote.toByteArray(),true,1,1))
    assertEquals(0,ReliableBridge.demand(h,remote.toByteArray()))
    val attempt=relayExecutions(checkNotNull(ReliableBridge.requests(h))).single{it.peerId==remote}.attempt
    assertEquals(attempt,ReliableBridge.open(h,attempt,false,remote.toByteArray()))
    var incoming=byteArrayOf();var offered=false;var finished=false;var largest=0
    val accepted=mutableSetOf<String>()
    val started=System.nanoTime()
    repeat(3500) {
     if(finished)return@repeat
     assertNotNull(ReliableBridge.exchange(h,attempt,ReliableBridge.RECEIVE,incoming))
     while(true){
      val event=checkNotNull(ReliableBridge.exchange(h,attempt,ReliableBridge.EVENT,byteArrayOf()))
      if(event.isEmpty())break
      if(NativeRecord(event,3).number()==0L)assertEquals(1.toByte(),checkNotNull(ReliableBridge.exchange(h,attempt,ReliableBridge.INITIAL_AUTHENTICATED_FRIEND_PIN,byteArrayOf())).first())
     }
     val status=checkNotNull(ReliableBridge.exchange(h,attempt,ReliableBridge.STATUS,byteArrayOf()))
     if(status[0]==1.toByte()&&!offered){
      val directory=NativeRecord(checkNotNull(TransferBridge.request(h,0,record())),30).text()
      val display="direct-日本-📷.bin";val filename="${UUID.randomUUID().toString().replace("-","")}-$display"
      File(directory,filename).outputStream().use{it.write(content(true));it.flush()}
      val command=NativeRecordWriter(30).apply{number(10);number(1);number(20);text(remote);number(1);text(filename);text(display)}.bytes()
      assertNotNull("Explicit online direct offer",TransferBridge.request(h,16,command));offered=true
     }
     val rows=attachmentSnapshots(checkNotNull(TransferBridge.request(h,4,record())))
     rows.filter{it.awaitingAcceptance&&accepted.add(it.id)}.forEach{assertNotNull(TransferBridge.request(h,2,record(it.id)))}
     rows.forEach{assertFalse("Unexpected transfer error: ${it.error}",it.state==6)}
     val outgoing=checkNotNull(ReliableBridge.exchange(h,attempt,ReliableBridge.OUTPUT,byteArrayOf()))
     assertTrue(outgoing.size<=65536);largest=maxOf(largest,outgoing.size)
     line("R:"+hex(outgoing));val response=read();require(response.startsWith("O:"));incoming=bytes(response.substring(2));val state=read()
     if(rows.size==2&&rows.all{it.complete}&&state=="S:1"&&incoming.isEmpty()){
      val received=rows.single{!it.outgoing};assertEquals(2*1024*1024+123L,received.done)
      assertArrayEquals(content(false),File(received.localPath).readBytes())
      line("V");assertEquals("VERIFIED",read());finished=true
     }
    }
    assertTrue("Both directions must complete with sender acknowledgments",finished)
    println("PASS real Android POSIX/Keystore ↔ Windows Schannel direct bytes_each_way=${2*1024*1024+123} maximum_opaque_bytes=$largest elapsed_ms=${(System.nanoTime()-started)/1_000_000}")
    assertEquals(0,ReliableBridge.close(h,attempt,false))
   }
  }} finally {
   check(name.startsWith("test_direct_"))
   val store=security.store();store.deleteEntry(AndroidRecipientKeyProvider(context,name).alias(1));store.deleteEntry(security.identityAlias);store.deleteEntry(security.storageAlias)
   val directory=File(context.noBackupFilesDir,"profiles/"+security.id.joinToString(""){"%02x".format(it)})
   check(!directory.exists()||directory.deleteRecursively());check(!security.directory.exists()||security.directory.deleteRecursively())
  }
 }
}
