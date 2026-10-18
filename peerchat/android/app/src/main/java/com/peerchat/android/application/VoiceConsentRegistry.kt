package com.peerchat.android.application

import java.util.UUID

/** One process-local, single-use explicit gesture. Access under the backend gate.
 * Neither a permission grant nor a remembered notification can recreate it. */
internal class VoiceConsentRegistry<T>(private val now:()->Long=System::nanoTime) {
    private data class Entry<T>(val consent:VoiceConsent,val binding:T,val expires:Long,var claimed:Boolean=false)
    private var pending:Entry<T>?=null
    fun create(peer:String,id:String,answer:Boolean,binding:T,group:String=""):VoiceConsent {
        check(pending==null||now()>=pending!!.expires){"A call action is already pending"}
        require(peer.matches(Regex("[0-9a-f]{64}"))&&id.matches(Regex("[0-9a-f]{32}")))
        require(group.isEmpty()||group.matches(Regex("[0-9a-f]{32}"))&&!answer)
        val consent=VoiceConsent(UUID.randomUUID().toString(),peer,id,answer,group)
        pending=Entry(consent,binding,now()+60_000_000_000L)
        return consent
    }
    fun valid(token:String):Boolean= pending?.let{it.consent.token==token&&now()<it.expires}==true
    fun claim(token:String):Pair<VoiceConsent,T> {
        val value=checkNotNull(pending?.takeIf{valid(token)&&!it.claimed}){"Call action expired or already used"}
        value.claimed=true
        return value.consent to value.binding
    }
    fun current(token:String,binding:T)=valid(token)&&pending?.binding===binding
    fun clear(token:String){if(pending?.consent?.token==token)pending=null}
    fun reset(){pending=null}
}
