package com.peerchat.android.platform

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.job.JobParameters
import android.app.job.JobService
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import com.peerchat.android.MainActivity
import com.peerchat.android.R
import com.peerchat.android.application.*
import kotlinx.coroutines.*

internal fun retryInterruptedWake(reason:Int):Boolean = reason !in setOf(
    JobParameters.STOP_REASON_CANCELLED_BY_APP,JobParameters.STOP_REASON_USER)

/** Bounded platform execution only. No new sockets, message store, retry owner,
 * or protocol. Native delivery/ACK/dedupe run in the shared leased backend. */
class PeerChatWakeJob:JobService() {
    private val scope=CoroutineScope(SupervisorJob()+Dispatchers.IO)
    private var work:Job?=null
    @Volatile private var execution=0L
    @Volatile private var urgentRequested=false
    // Main-thread job admission only: both scheduler purposes share ONE worker.
    private val admittedJobs=mutableMapOf<Int,JobParameters>()
    override fun onStartJob(params:JobParameters):Boolean {
        WakeSystemDiagnostics.record(this,"wake_job_started")
        OpaquePushWake.started(params.jobId)
        admittedJobs[params.jobId]=params
        if(work?.isActive!=true)urgentRequested=false
        if(params.jobId==OpaquePushWake.JOB_ID)urgentRequested=true
        if(work?.isActive==true){
            android.util.Log.i("PeerChatWake","stage=wake_running_coalesced job_id=${params.jobId}")
            return true
        }
        val admitted=++execution
        val started=System.nanoTime()
        val deadline=started+90_000_000_000L
        work=scope.launch {
            var lease:MessagingApplication?=null
            var peerLease:MessagingApplication?=null
            var sawCall=false
            var stage="configuration"
            var reschedule=true
            var lastOwnerState:String?=null
            fun recordOwnerState(final:Boolean=false) {
                val snapshot=lease?.state?.value ?: return
                val summary="authenticated=${snapshot.authenticated} presence_live=${snapshot.serverPresenceLive} contacts=${snapshot.conversations.size} ready_sessions=${snapshot.conversations.count{it.connection.ready}} history_rows=${snapshot.conversations.sumOf{it.messages.size}}"
                if(final||summary!=lastOwnerState){
                    android.util.Log.i("PeerChatWake","stage=wake_owner_state execution=$admitted final=$final $summary")
                    lastOwnerState=summary
                }
            }
            try{withTimeout(90000){
                val config=AndroidServerConfigurationStore(this@PeerChatWakeJob).load()
                if(config==null){reschedule=false;android.util.Log.i("PeerChatWake","stage=wake_exit reason=no_configuration");return@withTimeout}
                val credential=AndroidRememberedLoginStore(this@PeerChatWakeJob).load()
                if(credential==null){reschedule=false;android.util.Log.i("PeerChatWake","stage=wake_exit reason=no_remembered_credential");return@withTimeout}
                if(credential.revocationPending||credential.endpoint!=config){
                    reschedule=false
                    android.util.Log.i("PeerChatWake","stage=wake_exit reason=authorization_unavailable");return@withTimeout
                }
                android.util.Log.i("PeerChatWake","stage=remembered_credential_loaded")
                stage="remembered_authentication"
                lease=AndroidMessagingRuntime.acquire(this@PeerChatWakeJob,deadline,archiveLease=true)
                android.util.Log.i("PeerChatWake","stage=backend_lease_acquired")
                if(!lease!!.restoreRemembered(config)){
                    reschedule=false
                    android.util.Log.i("PeerChatWake","stage=wake_exit reason=remembered_restore_unavailable");return@withTimeout
                }
                stage="authenticated_delivery"
                android.util.Log.i("PeerChatWake","stage=authenticated_delivery")
                WakeSystemDiagnostics.power(this@PeerChatWakeJob,"authenticated_delivery_power")
                val preferences=AndroidQolStore(this@PeerChatWakeJob)
                val accountKey=accountPreferenceKey(config,credential.accountId)
                while(isActive){
                    // Opaque push is only a scheduling hint. Protected remembered
                    // authentication above and native peer authorization still
                    // own all signaling; this lease never grants microphone use.
                    if(urgentRequested&&peerLease==null){
                        peerLease=AndroidMessagingRuntime.acquire(this@PeerChatWakeJob,deadline,peerWakeLease=true)
                        android.util.Log.i("PeerChatWake","stage=authenticated_peer_wake")
                    }
                    recordOwnerState()
                    // Read completion BEFORE the snapshot used for notification
                    // planning. The owner gate drains the servicing/publish pass;
                    // an ACK arriving later must wait for the next iteration.
                    val outputDrained=lease!!.backgroundOutputDrained(started)
                    val snapshot=lease!!.state.value
                    val completed=runWakeNotificationStep(snapshot,credential.accountId,accountKey,
                        preferences,outputDrained){show(accountKey,it)}
                    // Real native receive/ACK completion is independent of OS
                    // notification permission, suppression or prior posting.
                    val incoming=snapshot.voice.direction==1L&&snapshot.voice.active
                    sawCall=sawCall||incoming
                    val elapsed=(System.nanoTime()-started)/1_000_000
                    val finished=backgroundWakeCanFinish(urgentRequested,completed,snapshot.voice.active,elapsed)
                    if(finished){
                        reschedule=!completed
                        android.util.Log.i("PeerChatWake","stage=wake_delivery_complete elapsed_ms="+(System.nanoTime()-started)/1_000_000)
                        return@withTimeout
                    }
                    delay(500)
                }
            }}catch(failure:Exception){
                recordOwnerState(final=true)
                // Fixed stage + class only. No tokens, server responses,
                // credentials, message contents or exception messages.
                android.util.Log.i("PeerChatWake","stage="+stage+" result="+if(failure is TimeoutCancellationException)"budget_complete" else failure.javaClass.simpleName)
            }
            finally{withContext(NonCancellable){
                peerLease?.close()
                lease?.close()
                val released=lease?.awaitQuiescence(2000) ?: true
                android.util.Log.i("PeerChatWake","stage=wake_job_finished reschedule=$reschedule quiescent=$released elapsed_ms="+(System.nanoTime()-started)/1_000_000)
                WakeSystemDiagnostics.power(this@PeerChatWakeJob,"wake_job_finished_power")
                withContext(Dispatchers.Main.immediate){
                    if(execution==admitted){
                        val retry=reschedule||!released
                        val jobs=admittedJobs.values.toList();admittedJobs.clear();work=null
                        for(job in jobs){
                            OpaquePushWake.finished(job.jobId)
                            // Never turn the urgent identity into a backed-off retry.
                            jobFinished(job,retry&&job.jobId==OpaquePushWake.RETRY_JOB_ID)
                        }
                        if(retry)OpaquePushWake.reconcile(this@PeerChatWakeJob)
                    }
                }
            }}
        };return true
    }
    private fun show(accountKey:String,value:LocalMessageNotification)=showMessageNotification(this,accountKey,value)
    override fun onStopJob(params:JobParameters):Boolean {
        WakeSystemDiagnostics.record(this,"wake_job_stopped",if(Build.VERSION.SDK_INT>=31)params.stopReason else -1)
        admittedJobs.remove(params.jobId);OpaquePushWake.finished(params.jobId)
        if(admittedJobs.isEmpty()){execution++;work?.cancel();work=null}
        // The platform owns constraint/backoff scheduling. Retain an interrupted
        // bounded wake, not a socket/service or a second messaging retry loop.
        // Every restart revalidates the protected authorization, including logout.
        val retry=Build.VERSION.SDK_INT<31||retryInterruptedWake(params.stopReason)
        if(retry&&params.jobId==OpaquePushWake.JOB_ID)OpaquePushWake.reconcile(this)
        return retry&&params.jobId==OpaquePushWake.RETRY_JOB_ID
    }
    override fun onDestroy(){execution++;admittedJobs.keys.forEach{OpaquePushWake.finished(it)};admittedJobs.clear();scope.cancel();super.onDestroy()}
}

/** One production wake iteration, including protected notification markers.
 * Completion is evaluated only after processing the published snapshot. */
internal fun runWakeNotificationStep(snapshot:MessagingSnapshot,expectedAccount:Long,
    accountKey:String,preferences:AndroidQolRepository,outputDrained:Boolean,
    show:(LocalMessageNotification)->Boolean):Boolean {
    if(snapshot.authenticated&&snapshot.serverPresenceLive&&snapshot.accountId==expectedAccount)
        publishWakeNotifications(snapshot,accountKey,preferences,show)
    return outputDrained
}

/** This is the same protected marker/notification path used by the JobService.
 * Fresh IDs originate only from native durable imports in this process. */
internal fun publishWakeNotifications(snapshot:MessagingSnapshot,accountKey:String,
    preferences:AndroidQolRepository,show:(LocalMessageNotification)->Boolean) {
    val saved=preferences.load()
    for(conversation in snapshot.conversations){
        val key=conversationPreferenceKey(accountKey,conversation.friend.accountId)
        val prior=saved.conversations[key]
        val incoming=conversation.messages.filter{it.senderDevice!=snapshot.localPeerId}
        val incomingMax=incoming.maxOfOrNull{it.sequence} ?: 0L
        val effective=if(prior!=null)prior else {
            val oldMax=incoming.filter{it.id !in snapshot.freshMailboxMessageIds}.maxOfOrNull{it.sequence} ?: 0L
            ConversationPreference(readSequence=oldMax,notifiedSequence=oldMax)
        }
        if(prior==null){
            preferences.update{state->if(state.conversations.size>=128||state.conversations.containsKey(key))state
                else state.copy(conversations=state.conversations+(key to effective))}
        }
        val planned=planMessageNotification(snapshot,conversation,effective,saved.preview)
        if(saved.notifications&&planned!=null&&show(planned)){
            preferences.update{state->val row=state.conversations[key] ?: effective
                state.copy(conversations=state.conversations+(key to row.copy(notifiedSequence=maxOf(row.notifiedSequence,incomingMax))))}
        }
    }
}

/** Keep an urgent hint alive for a bounded incoming signaling window, even when
 * mailbox work is empty or a short call ended: the server coalesces push for
 * 30 seconds, so quick repeats must still find a live receiver. Accepted calls
 * are retained by their own consent lease. */
internal fun backgroundWakeCanFinish(urgent:Boolean,drained:Boolean,callActive:Boolean,elapsedMs:Long):Boolean =
    if(!urgent)drained||elapsedMs>=30_000 else elapsedMs>=if(callActive)85_000 else 55_000
