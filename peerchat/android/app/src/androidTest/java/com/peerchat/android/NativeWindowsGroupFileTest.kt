package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.*
import com.peerchat.android.platform.*
import java.io.*
import java.net.Socket
import java.util.Base64
import java.util.UUID
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Isolated Android OS/Windows group-file fixture; all crypto and storage use shipping JNI. */
@RunWith(AndroidJUnit4::class)
class NativeWindowsGroupFileTest {
 @Test(timeout=360_000) fun bidirectionalGroupFiles() {
  val args=InstrumentationRegistry.getArguments();val port=args.getString("bridgePort")?.toIntOrNull()
  assumeTrue(port!=null)
  val context=InstrumentationRegistry.getInstrumentation().targetContext
  val name="test_group_files_${UUID.randomUUID()}"
  val security=ProfileSecurity(context,name);var profile=NativeProfile(context,name)
  val enc=Base64.getEncoder();val dec=Base64.getDecoder()
  var roster=byteArrayOf();var group="";var pin="";var server="";var attempt=0L
  val files=GroupAttachmentExecution()
  fun setup(){
   assertEquals(0,ReliableBridge.friends(profile.handle,3,1,NativeRecordWriter(1).apply{number(0)}.bytes()))
   assertEquals(0,ReliableBridge.network(profile.handle,1,1))
   assertEquals(0,MailboxBridge.configureEnrollment(profile.handle,3,1,server.toByteArray(),pin.toByteArray()))
   if(roster.isNotEmpty())assertNotNull(GroupBridge.request(profile.handle,1,roster))
  }
  fun network(op:Int,body:ByteArray=byteArrayOf()):ByteArray {
   val w=NativeRecordWriter(30);w.text("PeerChat/AndroidGroupAttachments/v1");w.number(3);w.number(1);w.raw(body)
   return checkNotNull(GroupAttachmentBridge.request(profile.handle,op,w.bytes()))
  }
  fun command(action:Int,id:String="",path:String="")=files.command(profile.handle,3,1,action,group,id,path)
  try {Socket("127.0.0.1",checkNotNull(port)).use{socket->
   socket.soTimeout=60_000
   val input=socket.getInputStream().bufferedReader(Charsets.US_ASCII);val out=PrintWriter(socket.getOutputStream().writer(Charsets.US_ASCII),true)
   fun send(b:ByteArray){out.println(enc.encodeToString(b))}
   send(checkNotNull(ProfileBridge.publicKey(profile.handle)))
   server="fixture.local:7443";pin="f".repeat(64);setup()
   val binding=checkNotNull(MailboxBridge.registration(profile.handle,true));send(binding)
   while(true){val parts=checkNotNull(input.readLine()).split('|',limit=4)
    val response=when(parts[0]){
     "G"->{val r=NativeRecord(dec.decode(parts[1]),30);server=r.text();pin=r.text();group=r.text();roster=r.bytes();r.end();setup();assertEquals(0,MailboxBridge.registered(profile.handle,binding));byteArrayOf(1)}
     "T"->{val w=NativeRecordWriter(27);w.text("PeerChat/AndroidGroups/v1");w.number(11);w.number(3);w.number(1);w.text(parts[1]);w.number(5);w.number(1);w.text(parts[2]);assertNotNull(GroupBridge.request(profile.handle,11,w.bytes()));val pending=relayExecutions(checkNotNull(ReliableBridge.requests(profile.handle))).single{it.peerId==parts[2]};attempt=pending.attempt;val local=PeerIdentityFormat.peerId(checkNotNull(ProfileBridge.publicKey(profile.handle)));assertEquals(attempt,ReliableBridge.open(profile.handle,attempt,local>parts[2],parts[2].toByteArray()));assertNotNull(ReliableBridge.exchange(profile.handle,attempt,ReliableBridge.RECEIVE,byteArrayOf()));byteArrayOf(1)}
     "E"->{val b=dec.decode(parts[1]);if(b.isNotEmpty())assertNotNull(ReliableBridge.exchange(profile.handle,attempt,ReliableBridge.RECEIVE,b));checkNotNull(ReliableBridge.exchange(profile.handle,attempt,ReliableBridge.OUTPUT,byteArrayOf()))}
     "C"->{assertNotNull(ReliableBridge.requests(profile.handle));network(1)}
     "R"->{network(2,dec.decode(parts[1]));byteArrayOf(1)}
     "V"->command(0).toByteArray()
     "A"->{command(2,parts[1]);byteArrayOf(1)}
     "K"->{command(3,parts[1]);byteArrayOf(1)}
     "O"->{val dir=NativeRecord(checkNotNull(TransferBridge.request(profile.handle,0,NativeRecordWriter(30).bytes())),30).text();val file=File(dir,"${UUID.randomUUID().toString().replace("-","")}-group-日本-📷.bin");file.writeBytes(ByteArray(256*1024+17){(it*37+23).toByte()});command(1,path=file.path).toByteArray()}
     "D"->{command(6,parts[1]);val rows=JSONObject(command(0)).getJSONArray("items");val row=(0 until rows.length()).map{rows.getJSONObject(it)}.single{it.getString("id")==parts[1]};val path=row.getString("path");if(path.isEmpty())byteArrayOf(0) else {val bytes=File(path).readBytes();assertArrayEquals(ByteArray(256*1024+17){(it*37+23).toByte()},bytes);byteArrayOf(1)}}
     "X"->{profile.close();profile=NativeProfile(context,name);setup();byteArrayOf(1)}
     "Q"->{send(byteArrayOf(1));break}
     else->error("Unknown group test command")
    };send(response)
   }
  }}finally{profile.close();val provider=AndroidRecipientKeyProvider(context,name);security.store().deleteEntry(provider.alias(1));security.store().deleteEntry(security.identityAlias);security.store().deleteEntry(security.storageAlias);val directory=File(context.noBackupFilesDir,"profiles/"+security.id.joinToString(""){"%02x".format(it)});check(name.startsWith("test_group_files_"));check(!directory.exists()||directory.deleteRecursively());check(!security.directory.exists()||security.directory.deleteRecursively())}
 }
}
