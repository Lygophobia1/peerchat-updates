package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.messageSnapshots
import com.peerchat.android.platform.AndroidStoragePlatform
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream
import java.security.MessageDigest

/** Emulator-only, local synthetic history. No server accounts, trust changes or network sends. */
@RunWith(AndroidJUnit4::class)
class HistoryPressureTest {
 @Test fun nineConversations() {
  val instrumentation=InstrumentationRegistry.getInstrumentation()
  val context=instrumentation.targetContext
  check(context.packageName=="com.peerchat.android.voiceintegration")
  check(android.os.Build.MODEL.contains("sdk"))
  val args=InstrumentationRegistry.getArguments()
  val label=args.getString("pressureLabel")?:"baseline"
  require(label.matches(Regex("[a-z0-9-]{1,24}")))
  val profile="test_history_pressure_$label-${System.nanoTime()}"
  val peers=(1..9).map{it.toString(16).padStart(64,'0')}
  fun report(s:String)=instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream","HISTORY_PRESSURE label=$label $s\n")})
  fun now()=System.nanoTime()
  fun ms(start:Long)=(now()-start)/1_000_000
  fun wire(id:String,sender:String,recipient:String,order:Int):ByteArray {
   val out=ByteArrayOutputStream();out.write(byteArrayOf(80,67,83,1))
   fun number(n:Long){repeat(8){out.write((n ushr (it*8)).toInt() and 255)}}
   fun text(s:String){val b=s.toByteArray();number(b.size.toLong());out.write(b)}
   text(id);text(sender);text(recipient);text("Generated history pressure fixture "+"x".repeat(240));number(1700000000000L+order);number(3);number(order.toLong())
   return out.toByteArray()
  }
  NativeProfile(context,profile).close() // Native creates the real schema.
  for(count in listOf(8,1000)) {
   val seed=now()
   AndroidStoragePlatform(context,profile).use{db->
    val local=MessageDigest.getInstance("SHA-256").digest(db.publicKey()).joinToString(""){"%02x".format(it)}
    for((chat,peer) in peers.withIndex()) {
     val query=db.prepare("SELECT COUNT(*) FROM messages WHERE peer=?".toByteArray())
     db.bindBytes(query,1,peer.toByteArray(),false);check(db.row(query));val existing=db.number(query,0).toInt();db.finish(query)
     for(start in existing until count step 100) {
      db.execute("BEGIN IMMEDIATE".toByteArray())
      try {
       for(index in start until minOf(start+100,count)) {
        val id=(chat+1).toString(16).padStart(8,'0')+(index+1).toString(16).padStart(24,'0')
        val plain=wire(id,peer,local,index+1)
        val encrypted=db.protect(plain,"PeerChat.v1/message/$id/$peer".toByteArray());plain.fill(0)
        val insert=db.prepare("INSERT INTO messages(id,peer,b) VALUES(?,?,?)".toByteArray())
        try{db.bindBytes(insert,1,id.toByteArray(),false);db.bindBytes(insert,2,peer.toByteArray(),false);db.bindBytes(insert,3,encrypted,true);db.row(insert)}finally{db.finish(insert)}
       }
       db.execute("COMMIT".toByteArray())
      }catch(e:Throwable){db.execute("ROLLBACK".toByteArray());throw e}
     }
    }
   }
   report("seed_per_chat=$count seed_ms=${ms(seed)}")
   NativeProfile(context,profile).use{native->
    val reader=BackgroundHistoryReader(context,profile)
    repeat(3){round->
     val start=now();val times=mutableListOf<Long>()
     val probes=if(count==1000&&round==0) java.util.concurrent.Executors.newSingleThreadExecutor() else null
     val probe=probes?.submit<String>{
      Thread.sleep(300)
      val begin=now();check(ProfileBridge.storageTest(native.handle,0)==0){"Message storage probe failed"};val messageMs=ms(begin);report("concurrent_message_queue_ms=$messageMs")
      val transferStart=now();check(ProfileBridge.attachmentTest(native.handle)==0){"Attachment file probe failed"}
      val result="message_queue_ms=$messageMs attachment_crypto_file_ms=${ms(transferStart)} finished_during_scan_ms=${ms(start)}"
      report("concurrent_completed $result");result
     }
     for(peer in peers){val one=now();val rows=messageSnapshots(reader.read(native.handle,peer,Long.MAX_VALUE));check(rows.size==8);times.add(ms(one));if(count==1000&&round==0)report("first_scan_chat=${times.size} elapsed_ms=${ms(start)}")}
     if(probe!=null){try{check(probe.isDone){"Live work waited for the entire history scan"};report("concurrent "+probe.get(30,java.util.concurrent.TimeUnit.SECONDS))}finally{probes.shutdownNow()}}
     report("per_chat=$count round=$round nine_page_ms=${ms(start)} per_page_ms=${times.joinToString(",")}")
    }
    val start=now()
    repeat(3){check(messageSnapshots(reader.read(native.handle,peers.last(),Long.MAX_VALUE)).size==8)}
    report("per_chat=$count same_chat_three_pages_ms=${ms(start)}")
   }
   NativeProfile(context,profile).use{reopened->
    val reader=BackgroundHistoryReader(context,profile);val start=now()
    for(peer in peers)check(messageSnapshots(reader.read(reopened.handle,peer,Long.MAX_VALUE)).size==8)
    val elapsed=ms(start);report("reopen_per_chat=$count nine_page_ms=$elapsed")
    check(elapsed<15000){"Reopen unexpectedly rescanned all message bodies"}
   }
  }
 }
}
