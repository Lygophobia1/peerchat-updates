package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.NativeRecord
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.platform.ProfileSecurity
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

/** Actual production JNI gate only. No microphone, synthetic media or E2E claim. */
@RunWith(AndroidJUnit4::class)
class NativeVoiceApplicationTest {
    private val context get()=InstrumentationRegistry.getInstrumentation().targetContext
    private val account=71L
    private val peer="a".repeat(64)
    private val call="1".repeat(32)
    private fun request(h:Long,op:Int,generation:Long=1,accountId:Long=account,body:(NativeRecordWriter)->Unit={}):ByteArray? {
        val w=NativeRecordWriter(7);w.text("PeerChat/AndroidVoice/v1");w.number(op.toLong());w.number(accountId);w.number(generation);body(w)
        return VoiceBridge.request(h,op,w.bytes())
    }
    private fun assertUnavailable(bytes:ByteArray?,op:Int,accepted:Boolean){
        val r=NativeRecord(checkNotNull(bytes),7);assertEquals("PeerChat/AndroidVoice/v1",r.text());assertEquals(op.toLong(),r.number())
        assertEquals(if(accepted)1L else 0L,r.number());assertEquals(0L,r.number())
        assertEquals("Pinned Android WebRTC NetEq receive library has not been supplied",r.text());assertEquals(0L,r.number());r.end()
    }
    private fun authorize(h:Long,generation:Long){
        val friends=NativeRecordWriter(1).apply{number(1);number(72);text(peer);number(1);text(peer)}.bytes()
        assertEquals(0,ReliableBridge.friends(h,account,generation,friends))
    }
    private fun clean(name:String){
        check(name.startsWith("test_native_voice_"));val security=ProfileSecurity(context,name)
        security.store().deleteEntry(security.identityAlias);security.store().deleteEntry(security.storageAlias)
        val directory=File(context.noBackupFilesDir,"profiles/"+security.id.joinToString(""){"%02x".format(it)})
        check(!directory.exists()||directory.deleteRecursively());check(!security.directory.exists()||security.directory.deleteRecursively())
    }
    @Test fun unavailableActualMediaCannotCreateCallsAndStaleScopesAreRejected(){
        val name="test_native_voice_${UUID.randomUUID()}"
        try{
            repeat(2){
                val profile=NativeProfile(context,name);val handle=profile.handle
                try{
                    assertUnavailable(request(handle,0,accountId=0),0,true)
                    authorize(handle,1)
                    assertUnavailable(request(handle,0),0,true)
                    assertUnavailable(request(handle,1){it.text(peer);it.text(call)},1,false)
                    // No active lifetime may be targeted, even with otherwise
                    // valid fields. Legacy id-only / Boolean-only records fail
                    // closed after the private JNI record shape changes.
                    for(op in 2..6){
                        assertNull(request(handle,op){it.text(peer);it.text(call);it.number(1);if(op>=5)it.number(1)})
                        assertNull(request(handle,op){it.text(call)})
                        assertNull(request(handle,op){it.number(1)})
                        assertNull(request(handle,op){it.text("bad");it.text(call);it.number(1);if(op>=5)it.number(1)})
                        assertNull(request(handle,op){it.text(peer);it.text("bad");it.number(1);if(op>=5)it.number(1)})
                        assertNull(request(handle,op){it.text(peer);it.text(call);it.number(0);if(op>=5)it.number(1)})
                        assertNull(request(handle,op){it.text(peer);it.text(call)})
                        assertNull(request(handle,op){it.text(peer);it.text(call);it.number(1);it.number(2)})
                        assertUnavailable(request(handle,0),0,true)
                    }
                    assertUnavailable(request(handle,0),0,true)
                    assertNull(request(handle,0,generation=2));assertNull(request(handle,0,accountId=72))
                    authorize(handle,2)
                    assertNull(request(handle,1,generation=1){it.text(peer);it.text(call)})
                    assertUnavailable(request(handle,0,generation=2),0,true)
                    assertUnavailable(request(handle,1,generation=2){it.text(peer);it.text(call)},1,false)
                    assertNull(request(handle,1,generation=2){it.text(peer)})
                    assertUnavailable(request(handle,0,generation=2),0,true)
                    val empty=NativeRecordWriter(1).apply{number(0)}.bytes();assertEquals(0,ReliableBridge.friends(handle,0,3,empty))
                    assertNull(request(handle,1,generation=3){it.text(peer);it.text(call)})
                    assertUnavailable(request(handle,0,generation=3,accountId=0),0,true)
                }finally{profile.close()}
                assertNull(request(handle,0,generation=3,accountId=0))
            }
        }finally{clean(name)}
    }
}
