package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.NativeRecord
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.platform.PeerIdentityFormat
import com.peerchat.android.platform.ProfileSecurity
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.UUID

/** Real production JNI, signed ledger, PCGE and Android Keystore persistence.
 * This fixture owns only a fresh namespaced test profile and its aliases. */
@RunWith(AndroidJUnit4::class)
class NativeGroupApplicationTest {
    private val context get()=InstrumentationRegistry.getInstrumentation().targetContext
    private val account=71L
    private fun request(h:Long,op:Int,generation:Long=1,body:(NativeRecordWriter)->Unit={}):ByteArray? {
        val w=NativeRecordWriter(27);w.text("PeerChat/AndroidGroups/v1");w.number(op.toLong());w.number(account);w.number(generation);body(w)
        return GroupBridge.request(h,op,w.bytes())
    }
    private fun command(h:Long,op:Int,body:(NativeRecordWriter)->Unit={}):NativeRecord {
        val r=NativeRecord(checkNotNull(request(h,op,body=body)),27)
        assertEquals("PeerChat/AndroidGroups/v1",r.text());assertEquals(op.toLong(),r.number());return r
    }
    private fun authorize(h:Long) {
        val empty=NativeRecordWriter(1).apply{number(0)}.bytes()
        assertEquals(0,ReliableBridge.friends(h,account,1,empty))
    }
    private fun roster(h:Long,id:String,legacy:String,peer:String,proof:ByteArray) {
        val evidence=NativeRecordWriter(5).apply{text("PeerChat/group-roster-evidence/v1");blob(proof);number(0)}.bytes()
        command(h,1){w->w.number(1);w.text(id);w.text(legacy);w.number(1);w.text(peer);w.blob(evidence)
            w.number(1);w.number(account);w.text(peer);w.number(3);w.number(0)}.end()
    }
    private fun clean(name:String) {
        check(name.startsWith("test_native_group_"))
        val security=ProfileSecurity(context,name)
        security.store().deleteEntry(security.identityAlias);security.store().deleteEntry(security.storageAlias)
        val directory=File(context.noBackupFilesDir,"profiles/"+security.id.joinToString(""){"%02x".format(it)})
        check(!directory.exists()||directory.deleteRecursively());check(!security.directory.exists()||security.directory.deleteRecursively())
    }
    @Test fun signedOwnerCiphertextHistoryRetryAndRevocationSurviveReopen() {
        val name="test_native_group_${UUID.randomUUID()}"
        val text="JNI group 👋 🚀 e\u0301 日本語 Привет\nexact second line"
        var proof=byteArrayOf();var public=byteArrayOf();var wire=byteArrayOf()
        var group="";var legacy="";var peer="";var message=""
        try {
            repeat(2){cycle->
                val profile=NativeProfile(context,name);val handle=profile.handle
                try {
                    authorize(handle)
                    // Authentication alone must not restore the last live roster.
                    assertNull(request(handle,2))
                    if(cycle==0){
                        public=checkNotNull(ProfileBridge.publicKey(handle));peer=PeerIdentityFormat.peerId(public)
                        command(handle,1){it.number(0)}.end()
                        val created=command(handle,4){it.text("Native group 🚀");it.text("Synthetic owner")}
                        proof=created.bytes();created.end()
                        val event=NativeRecord(proof,5);event.text();legacy=event.text()
                        group=MessageDigest.getInstance("SHA-256").digest("PeerChat group roster v1|$legacy|$peer".toByteArray()).joinToString(""){"%02x".format(it)}.take(32)
                    } else assertArrayEquals(public,ProfileBridge.publicKey(handle))
                    roster(handle,group,legacy,peer,proof)
                    val meta=command(handle,2);assertEquals(1L,meta.number());assertEquals(group,meta.text());assertEquals(legacy,meta.text())
                    assertEquals(1L,meta.number());assertEquals("Native group 🚀",meta.text());assertEquals(1L,meta.number());assertEquals(1L,meta.number())
                    assertEquals(account,meta.number());assertEquals(peer,meta.text());assertEquals(3L,meta.number());assertEquals(0L,meta.number())
                    assertEquals("PeerChat/GroupHistoryRevision/v1",meta.text());assertTrue(meta.number()>=0);meta.end()
                    assertEquals(0,MailboxBridge.configureEnrollment(handle,account,1,"fixture.invalid:7443".toByteArray(),"a".repeat(64).toByteArray()))
                    val files=com.peerchat.android.application.GroupAttachmentExecution()
                    val before=org.json.JSONObject(files.command(handle,account,1,0,legacy))
                    if(cycle==0){
                        val transport=com.peerchat.android.application.DmAttachmentExecution()
                        transport.serviceOutsideGate(Any()){
                            files.borrow(handle,account,1,{operation,request->
                                assertEquals("cache_pending_keys",operation)
                                org.json.JSONObject().put("type","cache_result").put("requestId",request.getString("requestId"))
                                    .put("operation",operation).put("cacheVersion",1).put("state","accepted")
                                    .put("items",org.json.JSONArray()).put("cursor","").put("more",false)
                            },{true},{},{throw it})
                        }
                        assertTrue(org.json.JSONObject(files.command(handle,account,1,0,legacy)).getBoolean("supported"))
                        val location=NativeRecord(checkNotNull(TransferBridge.request(handle,0,NativeRecordWriter(30).bytes())),30)
                        val root=File(location.text());location.end()
                        val source=File(root,UUID.randomUUID().toString().replace("-","")+"-photo-日本.png")
                        source.outputStream().use{out->val block=ByteArray(32768){(it%251).toByte()};repeat(65){out.write(block)}}
                        val attachment=files.command(handle,account,1,1,legacy,path=source.path)
                        val preparing=org.json.JSONObject(files.command(handle,account,1,0,legacy)).getJSONArray("items").getJSONObject(0)
                        assertEquals("preparing",preparing.getString("state"));assertEquals(source.length(),preparing.getLong("size"))
                        assertEquals("ok",files.command(handle,account,1,3,legacy,attachment))
                        assertTrue(source.exists());assertFalse(root.listFiles()!!.any{it.name=="$attachment.part"})
                        val oversized=File(root,UUID.randomUUID().toString().replace("-","")+"-direct-only.bin")
                        java.io.RandomAccessFile(oversized,"rw").use{it.setLength(1536L*1024*1024+1)}
                        var refused=false;try{files.command(handle,account,1,1,legacy,path=oversized.path)}catch(_:IllegalStateException){refused=true}
                        assertTrue(refused);assertTrue(oversized.delete())
                    }else{
                        assertEquals("canceled",before.getJSONArray("items").getJSONObject(0).getString("state"))
                    }
                    if(cycle==0){
                        val sent=command(handle,5){it.text(group);it.text(text)};assertEquals("accepted",sent.text())
                        message=sent.text();assertEquals(1L,sent.number());wire=sent.bytes();sent.end()
                        assertFalse(wire.toString(Charsets.ISO_8859_1).contains("JNI group"))
                    }
                    val retry=command(handle,8){it.text(group);it.text(message)}
                    assertEquals(message,retry.text());assertEquals(1L,retry.number());assertArrayEquals(wire,retry.bytes());retry.end()
                    val duplicate=command(handle,6){it.text(group);it.text(message);it.blob(wire)}
                    assertEquals("duplicate_message",duplicate.text());duplicate.end()
                    val changed=wire.clone().also{it[it.lastIndex]=(it.last().toInt() xor 1).toByte()}
                    val tampered=command(handle,6){it.text(group);it.text(message);it.blob(changed)}
                    assertNotEquals("accepted",tampered.text());tampered.end()
                    assertNull(request(handle,6){it.text("0".repeat(32));it.text(message);it.blob(wire)})
                    val history=command(handle,3){it.text(group);it.number(Long.MAX_VALUE)}
                    assertEquals(1L,history.number());assertEquals(message,history.text());assertEquals(peer,history.text());assertEquals(account,history.number())
                    assertTrue(history.number()>0);assertTrue(history.number()>0);assertTrue(history.number()>0);history.text();assertEquals(text,history.text())
                    assertEquals("PeerChat/GroupHistoryPage/v1",history.text());assertEquals(1L,history.number())
                    assertEquals("PeerChat/GroupReferencePage/v1",history.text());assertEquals(0L,history.number())
                    assertEquals(0L,history.number());assertEquals(0L,history.number());history.end()
                    assertNull(request(handle,2,generation=2))
                    // A truncated complete snapshot must revoke old authority.
                    assertNull(request(handle,1){it.number(1)})
                    assertNull(request(handle,8){it.text(group);it.text(message)})
                    roster(handle,group,legacy,peer,proof)
                    command(handle,1){it.number(0)}.end()
                    assertNull(request(handle,8){it.text(group);it.text(message)})
                    assertNull(request(handle,5){it.text(group);it.text("revoked")})
                    val revoked=command(handle,6){it.text(group);it.text(message);it.blob(wire)}
                    assertEquals("roster_unavailable",revoked.text());revoked.end()
                } finally {profile.close()}
                assertNull(request(handle,2))
            }
        } finally {clean(name)}
    }
}
