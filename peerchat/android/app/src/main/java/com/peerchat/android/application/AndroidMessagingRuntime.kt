package com.peerchat.android.application

import android.content.Context

internal data class BackendLeasePolicy(val deadline:Long,val peerWork:Boolean)
internal fun backendLeasePolicy(active:List<Pair<Long?,Boolean>>):BackendLeasePolicy {
    val foreground=active.any{it.first==null}
    val deadline=if(foreground)Long.MAX_VALUE else active.filter{it.second}.mapNotNull{it.first}.maxOrNull()
        ?: active.mapNotNull{it.first}.minOrNull() ?: Long.MAX_VALUE
    return BackendLeasePolicy(deadline,foreground||active.any{it.second})
}

/** Process-local application lifetime coordination, not session/delivery policy.
 * UI and bounded wake work lease the SAME existing MessagingBackend/Profile.
 * The last active lease quiesces it; no immortal socket. */
object AndroidMessagingRuntime {
    private var backend:MessagingBackend?=null
    private val leases=mutableSetOf<Lease>()
    @Synchronized fun acquire(context:Context,wakeDeadlineNanos:Long?=null,transferLease:Boolean=false):MessagingApplication {
        val owner=backend ?: MessagingBackend(context.applicationContext).also{backend=it}
        require(!transferLease||wakeDeadlineNanos!=null)
        return Lease(owner,wakeDeadlineNanos,transferLease,context.applicationContext).also{leases.add(it);update()}
    }
    private fun update(){
        val active=leases.filter{it.active}
        val policy=backendLeasePolicy(active.map{it.wakeDeadline to it.transferLease})
        backend?.setInUse(active.isNotEmpty(),policy.deadline,policy.peerWork)
    }
    private class Lease(val owner:MessagingBackend,val wakeDeadline:Long?,val transferLease:Boolean,val context:Context):MessagingApplication by owner {
        @Volatile var active=true
        @Volatile var closed=false
        override suspend fun sendAttachment(peerId:String,uri:android.net.Uri):String {
            val id=owner.sendAttachment(peerId,uri)
            if(wakeDeadline==null&&active&&!closed)com.peerchat.android.platform.PeerChatTransferService.start(context,id)
            return id
        }
        override suspend fun attachmentAction(id:String,accept:Boolean) {
            owner.attachmentAction(id,accept)
            if(accept&&wakeDeadline==null&&active&&!closed)com.peerchat.android.platform.PeerChatTransferService.start(context,id)
        }
        override fun setForeground(active:Boolean)=synchronized(AndroidMessagingRuntime){
            if(!closed){this.active=active;update()}
        }
        override fun close()=synchronized(AndroidMessagingRuntime){
            if(!closed){closed=true;active=false;leases.remove(this);update()}
        }
    }
}
