package com.peerchat.android.application

import org.json.JSONObject

/** Own-account public metadata only. Names never participate in peer trust,
 * historical provenance, recipient selection or acknowledgement decisions. */
data class OwnDeviceMetadata(val peerId:String,val name:String,val revoked:Boolean,val current:Boolean)
data class OwnDeviceInventory(val account:Long,val epoch:String,val revision:Long,val devices:List<OwnDeviceMetadata>)

internal fun decodeOwnDeviceInventory(value:JSONObject,account:Long,device:String,epoch:String):OwnDeviceInventory {
    require(account>0&&device.matches(Regex("[0-9a-f]{64}"))&&epoch.matches(Regex("[0-9a-f]{32}")))
    require(value.getInt("deviceManagementVersion")==1&&value.getLong("accountId")==account&&value.getString("serverEpoch")==epoch)
    val revision=value.getLong("authorityRevision");require(revision>=0)
    val rows=value.getJSONArray("devices");require(rows.length() in 1..64)
    val devices=List(rows.length()){i->
        val row=rows.getJSONObject(i);val peer=row.getString("publicIdentity");val name=row.getString("name")
        val state=row.getString("state");val current=row.getBoolean("currentDevice")
        require(peer.matches(Regex("[0-9a-f]{64}"))&&state in setOf("active","revoked")&&current==(peer==device))
        require(name.length<=64&&name.toByteArray(Charsets.UTF_8).size<=256&&name.none{it.isISOControl()})
        OwnDeviceMetadata(peer,name,state=="revoked",current)
    }
    require(devices.map{it.peerId}.distinct().size==devices.size&&devices.count{it.current}==1)
    return OwnDeviceInventory(account,epoch,revision,devices)
}

/** Used under the existing backend gate. An authenticated event is a hint, not
 * a snapshot: it can require a newer revision but cannot grant any authority. */
internal class OwnDeviceInventoryProjection {
    private var account=0L
    private var epoch=""
    private var requiredRevision=0L
    var snapshot:OwnDeviceInventory?=null
        private set
    fun reset(){account=0;epoch="";requiredRevision=0;snapshot=null}
    fun bind(account:Long,epoch:String){
        require(account>0&&epoch.matches(Regex("[0-9a-f]{32}")))
        if(this.account!=account||this.epoch!=epoch){reset();this.account=account;this.epoch=epoch}
    }
    fun hint(account:Long,epoch:String,revision:Long):Boolean {
        if(this.account!=account||this.epoch!=epoch||revision<=0||revision<=maxOf(requiredRevision,snapshot?.revision?:0))return false
        requiredRevision=revision;return true
    }
    fun accept(value:OwnDeviceInventory):Boolean {
        if(value.account!=account||value.epoch!=epoch||value.revision<maxOf(requiredRevision,snapshot?.revision?:0))return false
        // Same revision cannot rewrite already observed names or authorization.
        val previous=snapshot
        if(previous!=null&&previous.revision==value.revision&&previous.devices!=value.devices)return false
        // Reactivation is deliberately not part of this protocol. A higher
        // metadata revision cannot silently erase an observed tombstone.
        if(previous?.devices?.any{old->old.revoked&&value.devices.none{it.peerId==old.peerId&&it.revoked}}==true)return false
        snapshot=value;return true
    }
}
