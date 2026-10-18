package com.peerchat.android.application

import com.peerchat.android.GroupVoiceBridge
import com.peerchat.android.platform.MediaTurnCredentials

/** Current server-authorized room participation. mediaGeneration is a strictly
 * increasing native lifetime within one authenticated profile generation. */
internal data class GroupNativeBinding(val group:String,val room:String,val epoch:String,
    val participation:String,val mediaGeneration:Long) {
    init {require(listOf(group,room,epoch,participation).all{it.matches(Regex("[0-9a-f]{32}"))}&&mediaGeneration>0)}
}
internal data class GroupNativeSource(val account:Long,val device:String,val participation:String) {
    init {require(account>0&&device.matches(Regex("[0-9a-f]{64}"))&&participation.matches(Regex("[0-9a-f]{32}")))}
    val key:String get()="$account:$device:$participation"
}
internal data class GroupNativeSignal(val sourceKey:String,val routeId:String,val generation:Long,
    val kind:String,val data:String,val mid:String="0") {
    init {require(sourceKey.matches(Regex("[1-9][0-9]*:[0-9a-f]{64}:[0-9a-f]{32}"))&&
        routeId.matches(Regex("[0-9a-f]{32}"))&&generation>0&&kind in setOf("offer","answer","candidate","close")&&
        data.toByteArray(Charsets.UTF_8).size<=(if(kind=="candidate")4096 else 32768)&&mid.length<=32)}
}
internal data class GroupNativeSnapshot(val accepted:Boolean,val available:Boolean,val diagnostic:String,
    val active:Boolean,val binding:GroupNativeBinding?=null,val revision:Long=0,val roster:Long=0,
    val muted:Boolean=false,val deafened:Boolean=false,val connectedRoutes:Long=0,val totalRoutes:Long=0,
    val encodedPackets:Long=0,val receivedPackets:Long=0,val mixedBlocks:Long=0,val underrunBlocks:Long=0,
    val signals:List<GroupNativeSignal> = emptyList())

/** One bounded local record codec. Native verifies the current authenticated
 * GroupApplication roster independently of Android's room projection.
 *
 * Request: type 7; domain, op, account, authenticationGeneration; for ops 1–8
 * group/room/roomEpoch/localParticipation/nativeMediaGeneration. Start adds
 * revision, rosterGeneration, source count+rows, then TURN. Reconcile adds
 * revision, rosterGeneration, source count+rows. Configure adds TURN. Signal
 * adds sourceKey, routeId, transportGeneration, kind, data, mid. Mute/deafen
 * add one boolean number. Other operations have no trailing fields.
 * TURN: host, port, username, password, expiry, credentialGeneration.
 * Response: type 7; domain, op, accepted, available, safeDiagnostic, active;
 * if active, binding tuple then revision, roster, muted, deafened; then six
 * nonnegative counters (connected/total routes, encoded/received packets,
 * mixed/underrun blocks); then bounded signal count+rows (sourceKey, routeId,
 * generation, kind, data, mid).
 */
internal class GroupVoiceExecution(private val native:(Long,Int,ByteArray)->ByteArray?={h,o,b->GroupVoiceBridge.request(h,o,b)}) {
    private fun binding(w:NativeRecordWriter,value:GroupNativeBinding){
        w.text(value.group);w.text(value.room);w.text(value.epoch);w.text(value.participation);w.number(value.mediaGeneration)
    }
    private fun writeSources(w:NativeRecordWriter,values:List<GroupNativeSource>){
        require(values.size<=32&&values.map{it.device}.toSet().size==values.size)
        w.number(values.size.toLong());values.forEach{w.number(it.account);w.text(it.device);w.text(it.participation)}
    }
    private fun turn(w:NativeRecordWriter,value:MediaTurnCredentials,generation:Long){
        require(generation>0&&value.usableAt(System.currentTimeMillis()/1000))
        w.text(value.host);w.number(value.port.toLong());w.text(value.username);w.text(value.password)
        w.number(value.expiresUtc);w.number(generation)
    }
    private fun command(handle:Long,op:Int,account:Long,authenticationGeneration:Long,
                        scope:GroupNativeBinding?=null,write:(NativeRecordWriter)->Unit={}):GroupNativeSnapshot {
        require(handle>0&&account>0&&authenticationGeneration>0&&op in 0..8&&((op==0)==(scope==null)))
        val w=NativeRecordWriter(7);w.text("PeerChat/AndroidGroupVoice/v1");w.number(op.toLong());
        w.number(account);w.number(authenticationGeneration);if(scope!=null)binding(w,scope);write(w)
        val r=NativeRecord(checkNotNull(native(handle,op,w.bytes())),7)
        require(r.text()=="PeerChat/AndroidGroupVoice/v1"&&r.number()==op.toLong())
        fun flag():Boolean {val raw=r.number();require(raw<=1);return raw==1L}
        val accepted=flag();val available=flag();val diagnostic=r.text();require(diagnostic.length<=160)
        val active=flag()
        val current=if(active)GroupNativeBinding(r.text(),r.text(),r.text(),r.text(),r.number()) else null
        val revision=if(active)r.number() else 0;val roster=if(active)r.number() else 0
        val muted=if(active)flag() else false;val deafened=if(active)flag() else false
        val connectedRoutes=r.number();val totalRoutes=r.number();val encodedPackets=r.number()
        val receivedPackets=r.number();val mixedBlocks=r.number();val underrunBlocks=r.number()
        require(connectedRoutes<=totalRoutes&&totalRoutes<=32&&(!active||available)&&
            (active||listOf(connectedRoutes,totalRoutes,encodedPackets,receivedPackets,mixedBlocks,underrunBlocks).all{it==0L}))
        val count=r.number();require(count<=8)
        val signals=List(count.toInt()){GroupNativeSignal(r.text(),r.text(),r.number(),r.text(),r.text(),r.text())}
        r.end();require(!active||available&&revision>0&&roster>0)
        return GroupNativeSnapshot(accepted,available,diagnostic,active,current,revision,roster,muted,deafened,
            connectedRoutes,totalRoutes,encodedPackets,receivedPackets,mixedBlocks,underrunBlocks,signals)
    }
    fun snapshot(handle:Long,account:Long,authenticationGeneration:Long)=command(handle,0,account,authenticationGeneration)
    fun start(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding,
              revision:Long,roster:Long,sources:List<GroupNativeSource>,credentials:MediaTurnCredentials,
              credentialGeneration:Long):GroupNativeSnapshot {
        require(revision>0&&roster>0)
        return command(handle,1,account,authenticationGeneration,scope){w->
            w.number(revision);w.number(roster);writeSources(w,sources);turn(w,credentials,credentialGeneration)}
    }
    fun reconcile(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding,
                  revision:Long,roster:Long,sources:List<GroupNativeSource>):GroupNativeSnapshot {
        require(revision>0&&roster>0)
        return command(handle,2,account,authenticationGeneration,scope){w->w.number(revision);w.number(roster);writeSources(w,sources)}
    }
    fun configure(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding,
                  credentials:MediaTurnCredentials,credentialGeneration:Long)=
        command(handle,3,account,authenticationGeneration,scope){turn(it,credentials,credentialGeneration)}
    fun signal(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding,value:GroupNativeSignal)=
        command(handle,4,account,authenticationGeneration,scope){w->w.text(value.sourceKey);w.text(value.routeId)
            w.number(value.generation);w.text(value.kind);w.text(value.data);w.text(value.mid)}
    fun takeSignals(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding)=
        command(handle,5,account,authenticationGeneration,scope)
    fun mute(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding,value:Boolean)=
        command(handle,6,account,authenticationGeneration,scope){it.number(if(value)1 else 0)}
    fun deafen(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding,value:Boolean)=
        command(handle,7,account,authenticationGeneration,scope){it.number(if(value)1 else 0)}
    fun stop(handle:Long,account:Long,authenticationGeneration:Long,scope:GroupNativeBinding)=
        command(handle,8,account,authenticationGeneration,scope)
}
