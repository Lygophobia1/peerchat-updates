package com.peerchat.android.application

/** A password session becomes usable only after signed enrollment and resume. */
internal data class ExplicitDeviceLogin<C,S>(val session:S,val transientCredential:C?)

internal fun <C,S> establishDeviceBoundLogin(
    remember:Boolean,
    enroll:()->C,
    save:(C)->Unit,
    retirePasswordSession:()->Unit,
    resume:(C)->S,
    revoke:(C)->Unit
):ExplicitDeviceLogin<C,S> {
    val credential=enroll()
    var saved=false
    try {
        if(remember){save(credential);saved=true}
        retirePasswordSession()
        return ExplicitDeviceLogin(resume(credential),if(remember)null else credential)
    }catch(failure:Exception){
        if(!saved)try{revoke(credential)}catch(_:Exception){}
        throw failure
    }
}
