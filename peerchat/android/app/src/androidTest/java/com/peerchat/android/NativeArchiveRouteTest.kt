package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.*
import com.peerchat.android.platform.ProfileSecurity
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

/** Actual production Profile/JNI/signed enrollment/RelayRequests/Keystore BC TLS.
 * Only namespaced test profiles; no normal identity, grants or history touched. */
@RunWith(AndroidJUnit4::class)
class NativeArchiveRouteTest {
    private val context get()=InstrumentationRegistry.getInstrumentation().targetContext
    private fun command(h:Long,op:Int,body:(NativeRecordWriter)->Unit={}):ByteArray {
        val w=NativeRecordWriter(31);w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(op.toLong());body(w)
        return checkNotNull(ArchiveBridge.request(h,op,w.bytes()))
    }
    private fun metadata(h:Long,local:String,revision:Long,descriptors:List<ByteArray>,grants:List<ByteArray>){command(h,2){w->
        w.number(8);w.number(1);w.number(revision);w.text(local);w.text("test-archive-route-epoch");w.number(descriptors.size.toLong())
        descriptors.forEach(w::blob);w.number(grants.size.toLong());grants.forEach{w.blob(it);w.number(1)}
    }}
    private fun clean(profile:String){
        check(profile.startsWith("test_archive_route_"))
        val state=ProfileSecurity(context,profile);state.store().deleteEntry(state.identityAlias);state.store().deleteEntry(state.storageAlias)
        val root=File(context.noBackupFilesDir,"profiles/"+state.id.joinToString(""){"%02x".format(it)})
        check(!root.exists()||root.deleteRecursively());check(!state.directory.exists()||state.directory.deleteRecursively())
    }
    @Test fun schedulerRegisteredArchiveAdoptsOneAttemptAndAuthenticates(){
        val names=listOf("test_archive_route_${UUID.randomUUID()}","test_archive_route_${UUID.randomUUID()}")
        var savedA:ByteArray?=null;var savedB:ByteArray?=null;var savedGrants=emptyList<ByteArray>()
        try{repeat(5){NativeProfile(context,names[0]).use{left->NativeProfile(context,names[1]).use{right->
            val ah=left.handle;val bh=right.handle
            val a=checkNotNull(ProfileBridge.publicKey(ah));val b=checkNotNull(ProfileBridge.publicKey(bh))
            if(savedA!=null){assertArrayEquals(savedA,a);assertArrayEquals(savedB,b)}else{savedA=a;savedB=b}
            val aid=com.peerchat.android.platform.PeerIdentityFormat.peerId(a);val bid=com.peerchat.android.platform.PeerIdentityFormat.peerId(b)
            val noFriends=NativeRecordWriter(1).apply{number(0)}.bytes()
            for(h in listOf(ah,bh)){assertEquals(0,ReliableBridge.friends(h,8,1,noFriends));assertEquals(0,ReliableBridge.network(h,1,1))}
            val descriptors=listOf(command(ah,1),command(bh,1));metadata(ah,aid,1,descriptors,savedGrants);metadata(bh,bid,1,descriptors,savedGrants)
            if(savedGrants.isEmpty()){
                for(h in listOf(ah,bh))command(h,4){it.text(aid);it.number(1)}
                savedGrants=listOf(aid,bid).map{target->command(ah,5){it.text(target);it.number(3);it.number(1)}}
            }
            metadata(ah,aid,2,descriptors,savedGrants);metadata(bh,bid,2,descriptors,savedGrants)
                command(bh,14){it.number(8);it.number(1)}
                command(bh,12){it.number(8);it.number(1);it.text(aid);it.number(1)}
                val reader=relayExecutions(checkNotNull(ReliableBridge.requests(bh))).single()
                val scope=checkNotNull(reader.archive)
                command(ah,15){it.number(8);it.number(1);it.blob(scope.binding())}
                val source=relayExecutions(checkNotNull(ReliableBridge.requests(ah))).single()
                val incoming=aid>bid
                assertEquals(source.attempt,ReliableBridge.open(ah,source.attempt,incoming,bid.toByteArray()))
                assertEquals(reader.attempt,ReliableBridge.open(bh,reader.attempt,!incoming,aid.toByteArray()))
                assertEquals(0L,ReliableBridge.open(bh,reader.attempt,!incoming,aid.toByteArray()))
                assertNotNull(ReliableBridge.exchange(ah,source.attempt,ReliableBridge.RECEIVE,byteArrayOf()))
                assertNotNull(ReliableBridge.exchange(bh,reader.attempt,ReliableBridge.RECEIVE,byteArrayOf()))
                for(n in 0 until 100){
                    val l=checkNotNull(ReliableBridge.exchange(ah,source.attempt,ReliableBridge.OUTPUT,byteArrayOf()))
                    val r=checkNotNull(ReliableBridge.exchange(bh,reader.attempt,ReliableBridge.OUTPUT,byteArrayOf()))
                    if(l.isNotEmpty())assertNotNull(ReliableBridge.exchange(bh,reader.attempt,ReliableBridge.RECEIVE,l))
                    if(r.isNotEmpty())assertNotNull(ReliableBridge.exchange(ah,source.attempt,ReliableBridge.RECEIVE,r))
                    if(l.isEmpty()&&r.isEmpty())break
                }
                assertEquals(1,checkNotNull(ReliableBridge.exchange(ah,source.attempt,ReliableBridge.STATUS,byteArrayOf()))[0].toInt())
                assertEquals(1,checkNotNull(ReliableBridge.exchange(bh,reader.attempt,ReliableBridge.STATUS,byteArrayOf()))[0].toInt())
                assertEquals(0,ReliableBridge.close(ah,source.attempt,false));assertEquals(0,ReliableBridge.close(bh,reader.attempt,false))
                assertArrayEquals(a,ProfileBridge.publicKey(ah));assertArrayEquals(b,ProfileBridge.publicKey(bh))
            }
        }}}finally{names.forEach(::clean)}
    }
}
