package com.peerchat.android.application

data class ArchiveDeviceSnapshot(val peerId:String,val local:Boolean,val approvedIssuer:Boolean,val mayRead:Boolean,val mayServe:Boolean,val revoked:Boolean=false,val authorized:Boolean=false,val grantedRead:Boolean=mayRead,val grantedServe:Boolean=mayServe,val approvalKnown:Boolean=false,val mayAuthorize:Boolean=false,val pairAuthorized:Boolean=false,val localGrantPresent:Boolean=false,val localGrantRead:Boolean=false,val localGrantServe:Boolean=false,val maySetUp:Boolean=false,val approvalIssuer:String=""){
    val authorizationState:String get()=if(authorized||mayRead||mayServe)"Authorized" else if(revoked)"Revoked" else "Not authorized"
    val approved:Boolean get()=approvalKnown && if(local)grantedRead||grantedServe else pairAuthorized
    val needsApproval:Boolean get()=approvalKnown&&!approved&&!localGrantPresent
    val primaryAction:ArchiveDeviceAction? get()=when {
        !approvalKnown->null
        local&&maySetUp->ArchiveDeviceAction.SetUp
        needsApproval&&mayAuthorize->ArchiveDeviceAction.Authorize
        local&&approvalIssuer.isNotEmpty()->ArchiveDeviceAction.ApproveIssuer
        else->null
    }
}
data class ArchiveEnrollmentSnapshot(val accountId:Long,val authenticationGeneration:Long,val localPeerId:String,val devices:List<ArchiveDeviceSnapshot>,val historySyncEnabled:Boolean=true)
enum class ArchiveDeviceAction { TrustAuthorizer, Authorize, Revoke, DisableAuthorizer, SetUp, ApproveIssuer }
data class ArchiveSyncProgress(val state:String="Not started",val processed:Long=0,val conversations:Long=0,val complete:Boolean=false,val running:Boolean=false,val source:String="",val attachments:Long=0,val received:Long=0,val historyRevision:Long=0,val lastSuccessful:Long=0,val scanGeneration:Long=0,val enabled:Boolean=true,val authenticationGeneration:Long=0,val deferred:Long=0,val rejectedBatches:Long=0,val activity:Int=-1,val lastResult:Int=0,val resultAt:Long=0,val resultImported:Long=0,val resultConversations:Long=0,val resultDeferred:Long=0,val resultSource:String="",val providedQueued:Long=0,val providers:List<String> = emptyList(),val projectionFresh:Boolean=true){
    val currentActivity:String get()=if(!projectionFresh)"Status unavailable — retry pending" else if(activity<0)displayState else listOf("Idle","Connecting","Importing","Waiting","Paused","Off","Failed")[activity]
    val lastResultSummary:String get()=if(lastResult==0)"No completed result recorded" else "${listOf("None","Full","Partial","Failed","Cancelled")[lastResult]} · $resultImported messages imported · $resultConversations conversations completed · $resultDeferred unresolved records"
    val displayState:String get(){
        if(!projectionFresh)return "Waiting"
        val value=state.lowercase()
        return when {
            value.startsWith("partial history")->"Partial"
            "failed" in value||"rejected" in value->"Error"
            "off" in value||"paused" in value->"Paused"
            "unavailable" in value||"waiting" in value||"timed out" in value->"Waiting"
            complete->"Complete"
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
