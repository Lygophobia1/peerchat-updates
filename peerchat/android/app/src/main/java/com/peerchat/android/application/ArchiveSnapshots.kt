package com.peerchat.android.application

data class ArchiveDeviceSnapshot(val peerId:String,val local:Boolean,val approvedIssuer:Boolean,val mayRead:Boolean,val mayServe:Boolean,val revoked:Boolean=false,val authorized:Boolean=false,val grantedRead:Boolean=mayRead,val grantedServe:Boolean=mayServe){
    val authorizationState:String get()=if(authorized||mayRead||mayServe)"Authorized" else if(revoked)"Revoked" else "Not authorized"
}
data class ArchiveEnrollmentSnapshot(val accountId:Long,val authenticationGeneration:Long,val localPeerId:String,val devices:List<ArchiveDeviceSnapshot>,val historySyncEnabled:Boolean=true)
enum class ArchiveDeviceAction { TrustAuthorizer, Authorize, Revoke, DisableAuthorizer }
data class ArchiveSyncProgress(val state:String="Not started",val processed:Long=0,val conversations:Long=0,val complete:Boolean=false,val running:Boolean=false,val source:String="",val attachments:Long=0,val received:Long=0,val historyRevision:Long=0,val lastSuccessful:Long=0,val scanGeneration:Long=0,val enabled:Boolean=true,val authenticationGeneration:Long=0){
    val displayState:String get(){
        val value=state.lowercase()
        return when {
            "failed" in value||"rejected" in value->"Error"
            "off" in value||"paused" in value->"Paused"
            complete->"Complete"
            "unavailable" in value||"waiting" in value||"timed out" in value->"Waiting"
            "synchronizing" in value->"Syncing"
            "connecting" in value->"Connecting"
            else->"Idle"
        }
    }
    val sourceState:String get()=when(displayState){"Syncing"->"Available";"Connecting"->"Connecting";else->"Unavailable"}
}
internal fun archiveDeviceSnapshots(bytes:ByteArray):List<ArchiveDeviceSnapshot> {
    val record=NativeRecord(bytes,31)
    require(record.text()=="PeerChat/ArchiveApplication/v1"&&record.number()==1L&&record.number()==3L)
    val count=record.number();require(count<=32)
    fun flag():Boolean=record.number().also {require(it<=1)}==1L
    val result=List(count.toInt()){
        val peer=record.text();require(peer.matches(Regex("[0-9a-f]{64}")))
        ArchiveDeviceSnapshot(peer,flag(),flag(),flag(),flag())
    }
    require(result.map{it.peerId}.distinct().size==result.size)
    val revoked=mutableSetOf<String>()
    val authorized=mutableSetOf<String>()
    if(record.hasRemaining()){
        require(record.text()=="PeerChat/ArchiveDeviceStatus/v1")
        val n=record.number();require(n<=count)
        repeat(n.toInt()){val peer=record.text();require(result.any{it.peerId==peer}&&revoked.add(peer))}
        val a=record.number();require(a<=count)
        repeat(a.toInt()){val peer=record.text();require(result.any{it.peerId==peer}&&authorized.add(peer))}
    }
    record.end();return result.map{it.copy(revoked=it.peerId in revoked,authorized=it.peerId in authorized)}
}
