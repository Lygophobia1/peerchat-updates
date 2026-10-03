package com.peerchat.android.application

import android.content.Context

/** Process-local application lifetime coordination, not session/delivery policy.
 * UI and bounded wake work lease the SAME existing MessagingBackend/Profile.
 * The last active lease quiesces it; no foreground service/immortal socket. */
object AndroidMessagingRuntime {
    private var backend:MessagingBackend?=null
    private val leases=mutableSetOf<Lease>()
    @Synchronized fun acquire(context:Context):MessagingApplication {
        val owner=backend ?: MessagingBackend(context.applicationContext).also{backend=it}
        return Lease(owner).also{leases.add(it);update()}
    }
    private fun update(){backend?.setInUse(leases.any{it.active})}
    private class Lease(val owner:MessagingBackend):MessagingApplication by owner {
        var active=true;var closed=false
        override fun setForeground(active:Boolean)=synchronized(AndroidMessagingRuntime){
            if(!closed){this.active=active;update()}
        }
        override fun close()=synchronized(AndroidMessagingRuntime){
            if(!closed){closed=true;active=false;leases.remove(this);update()}
        }
    }
}
