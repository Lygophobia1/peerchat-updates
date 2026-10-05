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
    // Main-thread job admission only: both scheduler purposes share ONE worker.
    private val admittedJobs=mutableMapOf<Int,JobParameters>()
    override fun onStartJob(params:JobParameters):Boolean {
        WakeSystemDiagnostics.record(this,"wake_job_started")
        OpaquePushWake.started(params.jobId)
        admittedJobs[params.jobId]=params
        if(work?.isActive==true){
            android.util.Log.i("PeerChatWake","stage=wake_running_coalesced job_id=${params.jobId}")
            return true
        }
        val admitted=++execution
        val deadline=System.nanoTime()+30_000_000_000L
        work=scope.launch {
            var lease:MessagingApplication?=null
            var stage="configuration"
            var reschedule=true
            var notificationAt=0L
            var lastOwnerState:String?=null
            fun recordOwnerState(final:Boolean=false) {
                val snapshot=lease?.state?.value ?: return
                val summary="authenticated=${snapshot.authenticated} presence_live=${snapshot.serverPresenceLive} contacts=${snapshot.conversations.size} ready_sessions=${snapshot.conversations.count{it.connection.ready}} history_rows=${snapshot.conversations.sumOf{it.messages.size}}"
                if(final||summary!=lastOwnerState){
                    android.util.Log.i("PeerChatWake","stage=wake_owner_state execution=$admitted final=$final $summary")
                    lastOwnerState=summary
                }
            }
            try{withTimeout(30000){
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
                lease=AndroidMessagingRuntime.acquire(this@PeerChatWakeJob,deadline)
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
                    recordOwnerState()
                    val snapshot=lease!!.state.value
                    if(snapshot.authenticated&&snapshot.serverPresenceLive&&snapshot.accountId==credential.accountId){
                        val saved=preferences.load()
                        for(conversation in snapshot.conversations){
                            val key=conversationPreferenceKey(accountKey,conversation.friend.accountId)
                            val prior=saved.conversations[key]
                            val incomingMax=conversation.messages.filter{it.senderDevice!=snapshot.localPeerId}.maxOfOrNull{it.sequence} ?: 0L
                            if(prior==null){
                                preferences.update{state->if(state.conversations.size>=128||state.conversations.containsKey(key))state else state.copy(conversations=state.conversations+(key to ConversationPreference(readSequence=incomingMax,notifiedSequence=incomingMax)))}
                                continue
                            }
                            val planned=planMessageNotification(snapshot,conversation,prior,saved.preview)
                            if(saved.notifications&&planned!=null&&show(accountKey,planned)){
                                notificationAt=System.nanoTime()
                                preferences.update{state->val row=state.conversations[key] ?: prior;state.copy(conversations=state.conversations+(key to row.copy(notifiedSequence=maxOf(row.notifiedSequence,incomingMax))))}
                            }
                        }
                    }
                    if(notificationAt!=0L&&lease!!.backgroundOutputDrained(deadline-30_000_000_000L)){
                        reschedule=false
                        android.util.Log.i("PeerChatWake","stage=wake_delivery_complete elapsed_ms="+(30_000_000_000L-(deadline-System.nanoTime()))/1_000_000)
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
                lease?.close()
                val released=lease?.awaitQuiescence(2000) ?: true
                android.util.Log.i("PeerChatWake","stage=wake_job_finished reschedule=$reschedule quiescent=$released elapsed_ms="+(30_000_000_000L-(deadline-System.nanoTime()))/1_000_000)
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
    private fun show(accountKey:String,value:LocalMessageNotification):Boolean {
        android.util.Log.i("PeerChatWake","stage=notification_attempt")
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){
            android.util.Log.i("PeerChatWake","stage=notification_unavailable reason=permission");return false
        }
        val manager=getSystemService(NotificationManager::class.java);if(!manager.areNotificationsEnabled()){
            android.util.Log.i("PeerChatWake","stage=notification_unavailable reason=app_disabled");return false
        }
        manager.createNotificationChannel(NotificationChannel("messages","Messages",NotificationManager.IMPORTANCE_DEFAULT))
        val id=(accountKey+value.conversationId).hashCode()
        val intent=Intent(this,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            .putExtra("peerchat_account_key",accountKey).putExtra("peerchat_conversation",value.conversationId)
        val pending=PendingIntent.getActivity(this,id,intent,PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val notification=Notification.Builder(this,"messages").setSmallIcon(R.drawable.peerchat_icon)
            .setContentTitle(value.title).setContentText(value.body).setContentIntent(pending).setAutoCancel(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE).setNumber(value.count).setGroup("peerchat_messages").build()
        manager.notify(id,notification)
        android.util.Log.i("PeerChatWake","stage=notification_posted")
        WakeSystemDiagnostics.power(this,"notification_posted_power")
        return true
    }
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
