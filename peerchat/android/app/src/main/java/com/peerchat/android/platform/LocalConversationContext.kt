package com.peerchat.android.platform

import android.content.Context
import com.peerchat.android.application.FriendIdentity
import com.peerchat.android.application.reconcileFriends
import com.peerchat.android.application.deviceDisplayNames
import org.json.JSONArray
import org.json.JSONObject

/** Protected display/account binding only. Not friendship authority, credentials,
 * a message database, or permission to open a transport. History stays native. */
internal data class LocalConversationContext(val account:Long,val username:String,val device:String,
    val friends:List<FriendIdentity>)

internal class ProtectedLocalConversationContextStore(private val read:()->ByteArray?,
    private val write:(ByteArray)->Unit,private val records:RecordProtector) {
    private val label="local-conversation-context-v1".toByteArray(Charsets.US_ASCII)
    fun load(endpoint:ServerEndpoint):LocalConversationContext? {
        val blob=read() ?: return null
        require(blob.size in 1..1048576)
        val value=JSONObject(records.unprotect(blob,label).toString(Charsets.UTF_8))
        require(value.getInt("version")==1)
        if(value.getString("host")!=endpoint.host||value.getInt("port")!=endpoint.port||value.getString("pin")!=endpoint.certificatePin)return null
        val account=value.getLong("account");val name=value.getString("username");val device=value.getString("device")
        require(account>0&&name.length<=64&&device.matches(Regex("[0-9a-f]{64}")))
        val list=value.getJSONArray("friends");require(list.length()<=128)
        val friends=List(list.length()){i->val item=list.getJSONObject(i);val ids=item.getJSONArray("devices");require(ids.length() in 1..32)
            FriendIdentity(item.getLong("account"),item.getString("username"),item.getString("peer"),false,List(ids.length()){ids.getString(it)},deviceNames=deviceDisplayNames(item.opt("deviceNames")))}
        return LocalConversationContext(account,name,device,reconcileFriends(friends,friends))
    }
    fun save(endpoint:ServerEndpoint,value:LocalConversationContext) {
        require(validStoredServer(endpoint)&&value.account>0&&value.username.length<=64&&value.device.matches(Regex("[0-9a-f]{64}")))
        require(value.friends.size<=128);reconcileFriends(value.friends,value.friends)
        val list=JSONArray()
        value.friends.forEach{friend->list.put(JSONObject().put("account",friend.accountId).put("username",friend.username)
            .put("peer",friend.peerId).put("devices",JSONArray(friend.knownPeerIds)).put("deviceNames",JSONObject(friend.deviceNames?:emptyMap<String,String>())))}
        val plain=JSONObject().put("version",1).put("host",endpoint.host).put("port",endpoint.port).put("pin",endpoint.certificatePin)
            .put("account",value.account).put("username",value.username).put("device",value.device).put("friends",list).toString().toByteArray(Charsets.UTF_8)
        require(plain.size<=1048000)
        write(records.protect(plain,label))
    }
}
internal class LocalConversationContextStore(context:Context,profile:String) {
    private val state=ProfileSecurity(context,profile)
    private val delegate=ProtectedLocalConversationContextStore({state.locked{state.read("local-conversations.pcr")}},
        {blob->state.locked{state.write("local-conversations.pcr",blob)}},AndroidRecordProtector(context,profile))
    fun load(endpoint:ServerEndpoint)=delegate.load(endpoint)
    fun save(endpoint:ServerEndpoint,value:LocalConversationContext)=delegate.save(endpoint,value)
}
