package com.peerchat.android.platform

import android.content.Context
import com.peerchat.android.application.FriendIdentity
import com.peerchat.android.application.reconcileFriends
import com.peerchat.android.application.deviceDisplayNames
import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest

// Counterpart of the Desktop protected FriendConversationIndex: display and
// historical public device bindings only, not a message DB or trust owner.
internal class ProtectedFriendIdentityIndex(private val localAccount:Long,private val label:ByteArray,
    private val read:()->ByteArray?,private val write:(ByteArray)->Unit,private val records:RecordProtector) {
    private var previous: List<FriendIdentity>
    init {
        val stored=read()
        previous=if(stored==null)emptyList() else {
            require(stored.size<=1048576)
            val json=JSONObject(records.unprotect(stored,label).toString(Charsets.UTF_8))
            require(json.getInt("version")==1&&json.getLong("localAccount")==localAccount)
            val list=json.getJSONArray("bindings");require(list.length()<=1024)
            List(list.length()) { index ->
                val item=list.getJSONObject(index);val ids=item.getJSONArray("devices");require(ids.length() in 1..32)
                FriendIdentity(item.getLong("account"),item.getString("username"),item.getString("current"),false,
                    List(ids.length()){ids.getString(it)},deviceNames=deviceDisplayNames(item.opt("deviceNames")))
            }.also {reconcileFriends(it,it)}
        }
    }
    // Display/history recovery only. Includes historical removed bindings and
    // therefore MUST NOT be installed as authenticated friendship authority.
    fun localHistoryBindings():List<FriendIdentity> = previous.map{it.copy(online=false)}
    fun reconcile(current: List<FriendIdentity>): List<FriendIdentity> {
        val active=reconcileFriends(previous,current)
        // Retain removed account public bindings so re-adding it cannot bypass
        // historical-device verification. They do not grant active friendship.
        val all=(previous.filter {old->active.none {it.accountId==old.accountId}}+active).also {require(it.size<=1024)}
        val entries=JSONArray()
        all.forEach {friend->entries.put(JSONObject().put("account",friend.accountId).put("username",friend.username)
            .put("current",friend.peerId).put("devices",JSONArray(friend.knownPeerIds)).put("deviceNames",JSONObject(friend.deviceNames?:emptyMap<String,String>())))}
        val plain=JSONObject().put("version",1).put("localAccount",localAccount).put("bindings",entries).toString().toByteArray(Charsets.UTF_8)
        val protected=records.protect(plain,label)
        write(protected);previous=all
        return active
    }
}
internal class FriendIdentityIndex(context:Context,profile:String,endpoint:ServerEndpoint,localAccount:Long) {
    private val state=ProfileSecurity(context,profile)
    private val label="friend-index-v1:${endpoint.host}:${endpoint.port}:${endpoint.certificatePin}:$localAccount".toByteArray(Charsets.UTF_8)
    private val name="friends-"+MessageDigest.getInstance("SHA-256").digest(label).joinToString(""){"%02x".format(it)}+".pcr"
    private val delegate=ProtectedFriendIdentityIndex(localAccount,label,{state.locked{state.read(name)}},
        {bytes->state.locked{state.write(name,bytes)}},AndroidRecordProtector(context,profile))
    fun localHistoryBindings()=delegate.localHistoryBindings()
    fun reconcile(current:List<FriendIdentity>)=delegate.reconcile(current)
}
