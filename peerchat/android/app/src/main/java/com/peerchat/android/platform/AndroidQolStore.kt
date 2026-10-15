package com.peerchat.android.platform

import android.content.Context
import org.json.JSONObject
import java.security.MessageDigest

enum class NotificationPreview { SenderAndMessage, SenderOnly, HideContent }
data class ConversationPreference(val draft:String="",val readSequence:Long=0,val notifiedSequence:Long=0,val baselinePending:Boolean=false)
data class AndroidQolState(val preview:NotificationPreview=NotificationPreview.SenderOnly,
    val notifications:Boolean=true,val conversations:Map<String,ConversationPreference> = emptyMap(),val mailboxStorageAllowed:Boolean=true)
interface AndroidQolRepository {
    fun load():AndroidQolState
    fun save(value:AndroidQolState)
    fun update(transform:(AndroidQolState)->AndroidQolState){save(transform(load()))}
}
fun accountPreferenceKey(endpoint:ServerEndpoint,account:Long):String {
    require(account>0&&validStoredServer(endpoint))
    return MessageDigest.getInstance("SHA-256").digest("${endpoint.host}|${endpoint.port}|${endpoint.certificatePin}|$account".toByteArray(Charsets.UTF_8)).joinToString(""){"%02x".format(it)}
}
fun conversationPreferenceKey(accountKey:String,account:Long):String {
    require(accountKey.matches(Regex("[0-9a-f]{64}"))&&account>0);return "$accountKey:account:$account"
}
internal class ProtectedAndroidQolStore(private val read:()->ByteArray?,private val write:(ByteArray)->Unit,
    private val protector:()->RecordProtector,domain:String="android-qol-v1",
    private val keyPattern:Regex=Regex("[0-9a-f]{64}:account:[1-9][0-9]{0,18}")):AndroidQolRepository {
    private val label=domain.toByteArray(Charsets.US_ASCII)
    override fun load():AndroidQolState {
        val blob=read() ?: return AndroidQolState();require(blob.size in 1..1048576)
        val json=JSONObject(protector().unprotect(blob,label).toString(Charsets.UTF_8));require(json.getInt("version")==1)
        val rows=json.getJSONObject("conversations");require(rows.length()<=128)
        val values=rows.keys().asSequence().associateWith {key->
            require(key.matches(keyPattern))
            val r=rows.getJSONObject(key);ConversationPreference(r.getString("draft"),r.getLong("read"),r.getLong("notified"),if(r.has("baselinePending"))r.getBoolean("baselinePending") else false).also{require(it.draft.length<=4096&&it.readSequence>=0&&it.notifiedSequence>=0)}
        }
        // Absent is a product default, never an overwrite of explicit OFF. Malformed protected data still throws.
        return AndroidQolState(NotificationPreview.valueOf(json.getString("preview")),json.getBoolean("notifications"),values,if(json.has("mailboxStorageAllowed"))json.getBoolean("mailboxStorageAllowed") else true)
    }
    override fun save(value:AndroidQolState) {
        require(value.conversations.size<=128)
        val rows=JSONObject();value.conversations.forEach {key,r->
            require(key.matches(keyPattern)&&r.draft.length<=4096&&r.readSequence>=0&&r.notifiedSequence>=0)
            val row=JSONObject().put("draft",r.draft).put("read",r.readSequence).put("notified",r.notifiedSequence)
            if(r.baselinePending)row.put("baselinePending",true)
            rows.put(key,row)
        }
        val plain=JSONObject().put("version",1).put("preview",value.preview.name).put("notifications",value.notifications).put("mailboxStorageAllowed",value.mailboxStorageAllowed).put("conversations",rows).toString().toByteArray(Charsets.UTF_8)
        require(plain.size<=1000000);write(protector().protect(plain,label))
    }
}
fun groupPreferenceKey(accountKey:String,group:String):String {
    require(accountKey.matches(Regex("[0-9a-f]{64}"))&&group.matches(Regex("[0-9a-f]{32}")));return "$accountKey:group:$group"
}
// Same protected preference codec, separate domain/file. Older builds keep
// reading their unchanged DM preferences; this is never a group/message DB.
class AndroidGroupQolStore(context:Context,profile:String="default"):AndroidQolRepository {
    private val state=ProfileSecurity(context.applicationContext,profile)
    private val records by lazy{AndroidRecordProtector(context.applicationContext,profile)}
    private val delegate=ProtectedAndroidQolStore({state.locked{state.read("group-qol.pcr")}},
        {state.locked{state.write("group-qol.pcr",it)}},{records},"android-group-qol-v1",Regex("[0-9a-f]{64}:group:[0-9a-f]{32}"))
    override fun load()=synchronized(gate){delegate.load()}
    override fun save(value:AndroidQolState)=synchronized(gate){delegate.save(value)}
    override fun update(transform:(AndroidQolState)->AndroidQolState)=synchronized(gate){delegate.save(transform(delegate.load()))}
    companion object{private val gate=Any()}
}
class AndroidQolStore(context:Context,profile:String="default"):AndroidQolRepository {
    private val state=ProfileSecurity(context.applicationContext,profile)
    private val records by lazy{AndroidRecordProtector(context.applicationContext,profile)}
    private val delegate=ProtectedAndroidQolStore({state.locked{state.read("qol.pcr")}},
        {state.locked{state.write("qol.pcr",it)}},{records})
    override fun load()=synchronized(metadataGate){delegate.load()}
    override fun save(value:AndroidQolState)=synchronized(metadataGate){delegate.save(value)}
    // Cross-component metadata update stays atomic; history/outbox remain native.
    override fun update(transform:(AndroidQolState)->AndroidQolState)=synchronized(metadataGate){delegate.save(transform(delegate.load()))}
    companion object {private val metadataGate=Any()}
}
