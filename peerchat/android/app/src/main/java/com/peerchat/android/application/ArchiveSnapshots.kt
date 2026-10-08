package com.peerchat.android.application

data class ArchiveDeviceSnapshot(val peerId:String,val local:Boolean,val approvedIssuer:Boolean,val mayRead:Boolean,val mayServe:Boolean)
data class ArchiveEnrollmentSnapshot(val accountId:Long,val authenticationGeneration:Long,val localPeerId:String,val devices:List<ArchiveDeviceSnapshot>,val historySyncEnabled:Boolean=true)
enum class ArchiveDeviceAction { TrustAuthorizer, Authorize, Revoke, DisableAuthorizer }
internal fun archiveDeviceSnapshots(bytes:ByteArray):List<ArchiveDeviceSnapshot> {
    val record=NativeRecord(bytes,31)
    require(record.text()=="PeerChat/ArchiveApplication/v1"&&record.number()==1L&&record.number()==3L)
    val count=record.number();require(count<=32)
    fun flag():Boolean=record.number().also {require(it<=1)}==1L
    val result=List(count.toInt()){
        val peer=record.text();require(peer.matches(Regex("[0-9a-f]{64}")))
        ArchiveDeviceSnapshot(peer,flag(),flag(),flag(),flag())
    }
    record.end();require(result.map{it.peerId}.distinct().size==result.size);return result
}
