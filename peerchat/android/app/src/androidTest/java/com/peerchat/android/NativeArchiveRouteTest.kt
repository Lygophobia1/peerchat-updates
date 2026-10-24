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
    private fun progress(h:Long)=ArchiveEnrollmentExecution(
        {op,bytes->checkNotNull(ArchiveBridge.request(h,op,bytes))},
        {_,_->error("No server RPC belongs in the isolated archive fixture")}).progress(8,1)
    private fun history(h:Long,peer:String):List<MessageSnapshot>{
        val result=mutableListOf<MessageSnapshot>();var before=Long.MAX_VALUE
        repeat(16){
            val page=messageSnapshots(checkNotNull(ReliableBridge.history(h,peer.toByteArray(),before)))
            if(page.isEmpty())return result
            result.addAll(page);before=chronologicalMessages(page).first().sequence
        }
        error("Unbounded fixture history")
    }
    private fun sync(source:Long,reader:Long,sourceId:String,readerId:String){
        command(source,14){it.number(8);it.number(1)}
        command(reader,14){it.number(8);it.number(1)}
        command(reader,12){it.number(8);it.number(1);it.text(sourceId);it.number(1)}
        val r=relayExecutions(checkNotNull(ReliableBridge.requests(reader))).single{it.archive?.source==sourceId}
        command(source,15){it.number(8);it.number(1);it.blob(checkNotNull(r.archive).binding())}
        val s=relayExecutions(checkNotNull(ReliableBridge.requests(source))).single{it.archive?.source==sourceId}
        assertEquals(s.attempt,ReliableBridge.open(source,s.attempt,sourceId>readerId,readerId.toByteArray()))
        assertEquals(r.attempt,ReliableBridge.open(reader,r.attempt,readerId>sourceId,sourceId.toByteArray()))
        try{
            assertNotNull(ReliableBridge.exchange(source,s.attempt,ReliableBridge.RECEIVE,byteArrayOf()))
            assertNotNull(ReliableBridge.exchange(reader,r.attempt,ReliableBridge.RECEIVE,byteArrayOf()))
            repeat(200){
                // Production scheduler drives requests, not just TLS handshakes.
                assertNotNull(ReliableBridge.requests(source));assertNotNull(ReliableBridge.requests(reader))
                val outgoing=checkNotNull(ReliableBridge.exchange(source,s.attempt,ReliableBridge.OUTPUT,byteArrayOf()))
                val incoming=checkNotNull(ReliableBridge.exchange(reader,r.attempt,ReliableBridge.OUTPUT,byteArrayOf()))
                if(outgoing.isNotEmpty())assertNotNull(ReliableBridge.exchange(reader,r.attempt,ReliableBridge.RECEIVE,outgoing))
                if(incoming.isNotEmpty())assertNotNull(ReliableBridge.exchange(source,s.attempt,ReliableBridge.RECEIVE,incoming))
                if(progress(reader).complete)return
            }
            error("Nonempty JNI archive did not complete: ${progress(reader)}")
        }finally{
            assertEquals(0,ReliableBridge.close(source,s.attempt,false))
            assertEquals(0,ReliableBridge.close(reader,r.attempt,false))
        }
    }
    @Test fun nonemptyBidirectionalImportsSurviveReopenWithoutDuplicates(){
        val names=listOf("test_archive_route_${UUID.randomUUID()}","test_archive_route_${UUID.randomUUID()}")
        val contact="b".repeat(64)
        val friends=NativeRecordWriter(1).apply{number(1);number(9);text(contact);number(1);text(contact)}.bytes()
        val expected=mutableMapOf<String,Pair<String,String>>()
        var grants=emptyList<ByteArray>();var keys=emptyList<ByteArray>()
        try{repeat(3){cycle->NativeProfile(context,names[0]).use{left->NativeProfile(context,names[1]).use{right->
            val handles=listOf(left.handle,right.handle)
            val public=handles.map{checkNotNull(ProfileBridge.publicKey(it))}
            if(keys.isEmpty())keys=public else keys.zip(public).forEach{assertArrayEquals(it.first,it.second)}
            val ids=public.map{com.peerchat.android.platform.PeerIdentityFormat.peerId(it)}
            handles.forEach{assertEquals(0,ReliableBridge.friends(it,8,1,friends));assertEquals(0,ReliableBridge.network(it,1,1))}
            val descriptors=handles.map{command(it,1)}
            handles.forEachIndexed{i,h->metadata(h,ids[i],1,descriptors,grants)}
            if(grants.isEmpty()){
                handles.forEach{h->command(h,4){it.text(ids[0]);it.number(1)}}
                grants=ids.map{target->command(handles[0],5){it.text(target);it.number(3);it.number(1)}}
            }
            handles.forEachIndexed{i,h->metadata(h,ids[i],2,descriptors,grants)}
            if(cycle==0)handles.forEachIndexed{i,h->repeat(13){n->
                val text="Archive fixture $i/$n — 日本語 😀\nexact second line"
                val id=checkNotNull(ReliableBridge.queueLocal(h,contact.toByteArray(),text.toByteArray(Charsets.UTF_8))).toString(Charsets.US_ASCII)
                expected[id]=ids[i] to text
            }}
            sync(handles[0],handles[1],ids[0],ids[1])
            assertEquals(26,history(handles[1],contact).size)
            assertEquals(if(cycle==0)13L else 0L,progress(handles[1]).received)
            sync(handles[1],handles[0],ids[1],ids[0])
            assertEquals(26,history(handles[0],contact).size)
            assertEquals(if(cycle==0)13L else 0L,progress(handles[0]).received)
            handles.forEach{h->
                val rows=history(h,contact);assertEquals(26,rows.size);assertEquals(expected.keys,rows.map{it.id}.toSet())
                rows.forEach{m->assertEquals(expected.getValue(m.id).first,m.senderDevice);assertEquals(contact,m.recipientDevice);assertEquals(expected.getValue(m.id).second,m.text)}
            }
        }}}}finally{names.forEach(::clean)}
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
                // An unchanged authenticated FRIEND snapshot is not archive
                // revocation. Same-account archive devices are not friends.
                for(h in listOf(ah,bh))assertEquals(0,ReliableBridge.friends(h,8,1,noFriends))
                assertEquals(1,checkNotNull(ReliableBridge.exchange(ah,source.attempt,ReliableBridge.STATUS,byteArrayOf()))[0].toInt())
                assertEquals(1,checkNotNull(ReliableBridge.exchange(bh,reader.attempt,ReliableBridge.STATUS,byteArrayOf()))[0].toInt())
                // A real new authentication generation MUST invalidate them.
                for(h in listOf(ah,bh))assertEquals(0,ReliableBridge.friends(h,8,2,noFriends))
                assertEquals(0,checkNotNull(ReliableBridge.exchange(ah,source.attempt,ReliableBridge.STATUS,byteArrayOf()))[0].toInt())
                assertEquals(0,checkNotNull(ReliableBridge.exchange(bh,reader.attempt,ReliableBridge.STATUS,byteArrayOf()))[0].toInt())
                assertEquals(0,ReliableBridge.close(ah,source.attempt,false));assertEquals(0,ReliableBridge.close(bh,reader.attempt,false))
                assertArrayEquals(a,ProfileBridge.publicKey(ah));assertArrayEquals(b,ProfileBridge.publicKey(bh))
            }
        }}}finally{names.forEach(::clean)}
    }
}
