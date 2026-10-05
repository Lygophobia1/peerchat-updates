package com.peerchat.android.application

import android.content.Context

/** Process-local application lifetime coordination, not session/delivery policy.
 * UI and bounded wake work lease the SAME existing MessagingBackend/Profile.
 * The last active lease quiesces it; no foreground service/immortal socket. */
object AndroidMessagingRuntime {
    private var backend:MessagingBackend?=null
    private val leases=mutableSetOf<Lease>()
    @Synchronized fun acquire(context:Context,wakeDeadlineNanos:Long?=null):MessagingApplication {
        val owner=backend ?: MessagingBackend(context.applicationContext).also{backend=it}
        return Lease(owner,wakeDeadlineNanos).also{leases.add(it);update()}
    }
    private fun update(){
        val active=leases.filter{it.active}
        val deadline=if(active.any{it.wakeDeadline==null})Long.MAX_VALUE else active.mapNotNull{it.wakeDeadline}.minOrNull() ?: Long.MAX_VALUE
        backend?.setInUse(active.isNotEmpty(),deadline)
    }
    private class Lease(val owner:MessagingBackend,val wakeDeadline:Long?):MessagingApplication by owner {
        var active=true;var closed=false
        override fun setForeground(active:Boolean)=synchronized(AndroidMessagingRuntime){
            if(!closed){this.active=active;update()}
        }
        override fun close()=synchronized(AndroidMessagingRuntime){
            if(!closed){closed=true;active=false;leases.remove(this);update()}
        }
    }
}
