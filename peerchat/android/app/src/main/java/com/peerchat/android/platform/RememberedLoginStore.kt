package com.peerchat.android.platform

import android.content.Context
import org.json.JSONObject

// Never stringify this record: it contains an opaque authentication credential.
class RememberedLogin(val endpoint:ServerEndpoint,val accountId:Long,val device:String,
    internal val credential:String,val revocationPending:Boolean=false) {
    fun pendingRevocation()=RememberedLogin(endpoint,accountId,device,credential,true)
}
interface RememberedLoginRepository {
    fun load():RememberedLogin?
    fun save(value:RememberedLogin)
    fun clear()
}
internal class ProtectedRememberedLoginStore(private val read:()->ByteArray?,
    private val write:(ByteArray)->Unit,private val protector:()->RecordProtector):RememberedLoginRepository {
    private val label="remembered-login-v1".toByteArray(Charsets.US_ASCII)
    override fun load():RememberedLogin? {
        val blob=read() ?: return null
        require(blob.size in 1..4096)
        val plain=protector().unprotect(blob,label);require(plain.size in 1..2048)
        val json=JSONObject(plain.toString(Charsets.UTF_8));require(json.getInt("version")==1)
        if(!json.getBoolean("present")){require(json.length()==2);return null}
        require(json.length()==9)
        val value=RememberedLogin(ServerEndpoint(json.getString("host"),json.getInt("port"),json.getString("pin")),
            json.getLong("account"),json.getString("device"),json.getString("credential"),json.getBoolean("revoking"))
        validate(value);return value
    }
    private fun validate(value:RememberedLogin) {
        require(validStoredServer(value.endpoint)&&value.accountId>0&&value.device.matches(Regex("[0-9a-f]{64}"))&&
            value.credential.matches(Regex("[A-Za-z0-9_-]{43}")))
        // Automatic authentication always requires an explicitly trusted server.
        require(value.endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
    }
    override fun save(value:RememberedLogin) {
        validate(value)
        val json=JSONObject().put("version",1).put("present",true).put("host",value.endpoint.host)
            .put("port",value.endpoint.port).put("pin",value.endpoint.certificatePin).put("account",value.accountId)
            .put("device",value.device).put("credential",value.credential).put("revoking",value.revocationPending)
        write(protector().protect(json.toString().toByteArray(Charsets.UTF_8),label))
    }
    // Atomic authenticated tombstone avoids backup resurrection and unsafe deletion.
    override fun clear(){write(protector().protect(JSONObject().put("version",1).put("present",false).toString().toByteArray(Charsets.UTF_8),label))}
}
class AndroidRememberedLoginStore(context:Context,profile:String="default"):RememberedLoginRepository {
    private val state=ProfileSecurity(context.applicationContext,profile)
    private val records by lazy {AndroidRecordProtector(context.applicationContext,profile)}
    private val delegate=ProtectedRememberedLoginStore({state.locked{state.read("remembered-login.pcr")}},
        {state.locked{state.write("remembered-login.pcr",it)}},{records})
    override fun load()=delegate.load()
    override fun save(value:RememberedLogin)=delegate.save(value)
    override fun clear()=delegate.clear()
}
