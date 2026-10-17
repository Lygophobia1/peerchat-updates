package com.peerchat.android.application

import android.content.Context
import com.peerchat.android.platform.AccountSession
import com.peerchat.android.platform.ServerEndpoint
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

internal data class BackendLeasePolicy(val deadline:Long,val peerWork:Boolean,val archiveWork:Boolean=peerWork)
internal fun backendLeasePolicy(active:List<Pair<Long?,Boolean>>,archiveDeadlines:List<Long> = emptyList()):BackendLeasePolicy {
    val foreground=active.any{it.first==null}
    val deadline=if(foreground)Long.MAX_VALUE else active.filter{it.second}.mapNotNull{it.first}.maxOrNull()
        ?: active.mapNotNull{it.first}.minOrNull() ?: Long.MAX_VALUE
    val peerWork=foreground||active.any{it.second}
    return BackendLeasePolicy(if(foreground)deadline else maxOf(deadline,archiveDeadlines.maxOrNull()?:deadline),peerWork,peerWork||archiveDeadlines.isNotEmpty())
}

/** Process-local application lifetime coordination, not session/delivery policy.
 * UI and bounded wake work lease the SAME existing MessagingBackend/Profile.
 * The last active lease quiesces work; a stopped UI may briefly retain sockets. */
object AndroidMessagingRuntime {
    private var backend:MessagingBackend?=null
    private val leases=mutableSetOf<Lease>()
    private val grace=BackgroundConnectionGrace()
    private val timer=Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task,"peerchat-connection-grace").apply { isDaemon=true }
    }
    private var expiry:ScheduledFuture<*>?=null

    private fun cancelGrace() {
        expiry?.cancel(false);expiry=null;grace.cancel()
    }
    private fun scheduleGraceExpiry(token:Long,delayNanos:Long) {
        expiry=timer.schedule({ synchronized(this) {
            val now=System.nanoTime()
            if(grace.expire(token,now)) { expiry=null;update() }
            else grace.remaining(token,now).takeIf { it>0L }?.let { scheduleGraceExpiry(token,it) }
        } },delayNanos,TimeUnit.NANOSECONDS)
    }
    private fun beginGrace() {
        expiry?.cancel(false)
        val now=System.nanoTime()
        scheduleGraceExpiry(grace.begin(now),60_000_000_000L)
    }
    @Synchronized fun acquire(context:Context,wakeDeadlineNanos:Long?=null,transferLease:Boolean=false,archiveLease:Boolean=false):MessagingApplication {
        val owner=backend ?: MessagingBackend(context.applicationContext).also{backend=it}
        require(!(transferLease||archiveLease)||wakeDeadlineNanos!=null)
        if(wakeDeadlineNanos==null)cancelGrace()
        return Lease(owner,wakeDeadlineNanos,transferLease,archiveLease,context.applicationContext).also{leases.add(it);update()}
    }
    private fun update(){
        val active=leases.filter{it.active}
        val policy=backendLeasePolicy(active.map{it.wakeDeadline to it.transferLease},active.filter{it.archiveLease}.mapNotNull{it.wakeDeadline})
        backend?.setInUse(active.isNotEmpty(),policy.deadline,policy.peerWork,policy.archiveWork,grace.deadline(System.nanoTime()))
    }
    private class Lease(val owner:MessagingBackend,val wakeDeadline:Long?,val transferLease:Boolean,val archiveLease:Boolean,val context:Context):MessagingApplication by owner {
        @Volatile var active=true
        @Volatile var closed=false
        private fun endGraceForScopeChange()=synchronized(AndroidMessagingRuntime){
            cancelGrace();update()
        }
        override suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String):AccountSession {
            endGraceForScopeChange()
            return owner.signIn(endpoint,username,password)
        }
        override suspend fun signInRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession {
            endGraceForScopeChange()
            return owner.signInRemembered(endpoint,username,password,remember)
        }
        override suspend fun createAccountRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession {
            endGraceForScopeChange()
            return owner.createAccountRemembered(endpoint,username,password,remember)
        }
        override suspend fun restoreRemembered(endpoint:ServerEndpoint):Boolean {
            endGraceForScopeChange()
            return owner.restoreRemembered(endpoint)
        }
        override suspend fun resetNetworkSession() {
            endGraceForScopeChange()
            owner.resetNetworkSession()
        }
        override suspend fun sendAttachment(peerId:String,uri:android.net.Uri):String {
            val id=owner.sendAttachment(peerId,uri)
            if(wakeDeadline==null&&active&&!closed)com.peerchat.android.platform.PeerChatTransferService.start(context,id)
            return id
        }
        override suspend fun attachmentAction(id:String,accept:Boolean) {
            owner.attachmentAction(id,accept)
            if(accept&&wakeDeadline==null&&active&&!closed)com.peerchat.android.platform.PeerChatTransferService.start(context,id)
        }
        override suspend fun resolveAttachmentCompatibility(token:String,direct:Boolean):String? {
            val id=owner.resolveAttachmentCompatibility(token,direct)
            if(id!=null&&wakeDeadline==null&&active&&!closed)com.peerchat.android.platform.PeerChatTransferService.start(context,id)
            return id
        }
        override fun setForeground(active:Boolean)=synchronized(AndroidMessagingRuntime){
            if(!closed){
                val leavingForeground=this.active&&!active&&wakeDeadline==null
                this.active=active
                if(wakeDeadline==null) {
                    if(active)cancelGrace()
                    else if(leavingForeground&&leases.none { it.active&&it.wakeDeadline==null })beginGrace()
                }
                update()
                if(leavingForeground)scheduleArchiveContinuation()
            }
        }
        override suspend fun signOut() {
            endGraceForScopeChange()
            owner.signOut()
        }
        override fun close()=synchronized(AndroidMessagingRuntime){
            if(!closed){
                val leavingForeground=active&&wakeDeadline==null
                closed=true;active=false;leases.remove(this)
                if(wakeDeadline==null)cancelGrace()
                update()
                if(leavingForeground)scheduleArchiveContinuation()
            }
        }
        private fun scheduleArchiveContinuation(){
            if(archiveWakePending(owner.state.value.archiveProgress))try{
                com.peerchat.android.platform.OpaquePushWake.reconcile(context)
            }catch(_:Exception){/* Periodic/reconnect recovery remains available. */}
        }
    }
}
