package com.peerchat.android.platform

import android.content.Context
import org.json.JSONObject
import java.security.MessageDigest

enum class NotificationPreview { SenderAndMessage, SenderOnly, HideContent }
data class ConversationPreference(val draft:String="",val readSequence:Long=0,val notifiedSequence:Long=0)
data class AndroidQolState(val preview:NotificationPreview=NotificationPreview.SenderOnly,
    val notifications:Boolean=true,val conversations:Map<String,ConversationPreference> = emptyMap())
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
    private val protector:()->RecordProtector):AndroidQolRepository {
    private val label="android-qol-v1".toByteArray(Charsets.US_ASCII)
    override fun load():AndroidQolState {
        val blob=read() ?: return AndroidQolState();require(blob.size in 1..1048576)
        val json=JSONObject(protector().unprotect(blob,label).toString(Charsets.UTF_8));require(json.getInt("version")==1)
        val rows=json.getJSONObject("conversations");require(rows.length()<=128)
        val values=rows.keys().asSequence().associateWith {key->
            require(key.matches(Regex("[0-9a-f]{64}:account:[1-9][0-9]{0,18}")))
            val r=rows.getJSONObject(key);ConversationPreference(r.getString("draft"),r.getLong("read"),r.getLong("notified")).also{require(it.draft.length<=4096&&it.readSequence>=0&&it.notifiedSequence>=0)}
        }
        return AndroidQolState(NotificationPreview.valueOf(json.getString("preview")),json.getBoolean("notifications"),values)
    }
    override fun save(value:AndroidQolState) {
        require(value.conversations.size<=128)
        val rows=JSONObject();value.conversations.forEach {key,r->
            require(key.matches(Regex("[0-9a-f]{64}:account:[1-9][0-9]{0,18}"))&&r.draft.length<=4096&&r.readSequence>=0&&r.notifiedSequence>=0)
            rows.put(key,JSONObject().put("draft",r.draft).put("read",r.readSequence).put("notified",r.notifiedSequence))
        }
        val plain=JSONObject().put("version",1).put("preview",value.preview.name).put("notifications",value.notifications).put("conversations",rows).toString().toByteArray(Charsets.UTF_8)
        require(plain.size<=1000000);write(protector().protect(plain,label))
    }
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
