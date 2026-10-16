package com.peerchat.android.platform

import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context
import android.os.Build

/** Entry point for a future vendor SDK adapter. A hint contains no identity,
 * conversation or content. It cannot authenticate or create a notification. */
object OpaquePushWake {
    // Keep the old identity for its durable, possibly backed-off reconciliation.
    const val RETRY_JOB_ID=7341
    const val JOB_ID=7342
    const val PERIODIC_JOB_ID=7343
    private val running=mutableSetOf<Int>()
    @Synchronized internal fun started(id:Int){running.add(id)}
    @Synchronized internal fun finished(id:Int){running.remove(id)}
    internal fun equivalentImmediate(running:Boolean,pending:Boolean,minimumLatency:Long,delayedBySystem:Boolean)=
        running||(pending&&minimumLatency==0L&&!delayedBySystem)
    private fun delayed(scheduler:JobScheduler):Boolean {
        if(Build.VERSION.SDK_INT<34)return false
        return try {
            val reasons=if(Build.VERSION.SDK_INT>=36)
                JobScheduler::class.java.getMethod("getPendingJobReasons",Int::class.javaPrimitiveType).invoke(scheduler,JOB_ID) as IntArray
            else intArrayOf(scheduler.getPendingJobReason(JOB_ID))
            reasons.any{it!=JobScheduler.PENDING_JOB_REASON_UNDEFINED&&it!=JobScheduler.PENDING_JOB_REASON_EXECUTING}
        }catch(_:Exception){true} // Unknown pending state must not suppress a fresh hint.
    }
    @Synchronized internal fun reconcile(context:Context):Boolean {
        val scheduler=context.getSystemService(JobScheduler::class.java)
        if(scheduler.getPendingJob(RETRY_JOB_ID)!=null||running.contains(RETRY_JOB_ID))return true
        return scheduler.schedule(JobInfo.Builder(RETRY_JOB_ID,ComponentName(context,PeerChatWakeJob::class.java))
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setMinimumLatency(30000)
            .setBackoffCriteria(30000,JobInfo.BACKOFF_POLICY_EXPONENTIAL).build())==JobScheduler.RESULT_SUCCESS
    }
    /** One persisted, network-constrained recovery window for an opted-in
     * remembered profile. JobScheduler owns reboot and connectivity deferral. */
    @Synchronized internal fun maintainPeriodic(context:Context,enabled:Boolean):Boolean {
        val scheduler=context.getSystemService(JobScheduler::class.java)
        if(!enabled){scheduler.cancel(PERIODIC_JOB_ID);return true}
        if(scheduler.getPendingJob(PERIODIC_JOB_ID)!=null)return true
        return scheduler.schedule(JobInfo.Builder(PERIODIC_JOB_ID,ComponentName(context,PeerChatWakeJob::class.java))
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setPersisted(true)
            .setPeriodic(6*60*60*1000L,60*60*1000L).build())==JobScheduler.RESULT_SUCCESS
    }
    internal fun periodicEligible(config:ServerEndpoint?,credential:RememberedLogin?,allowed:Boolean):Boolean =
        config!=null&&credential!=null&&!credential.revocationPending&&credential.endpoint==config&&allowed
    /** Recheck protected authorization after credential/consent transitions.
     * Scheduling is best effort and never changes the underlying login result. */
    internal fun refreshPeriodic(context:Context):Boolean = try {
        val config=AndroidServerConfigurationStore(context).load()
        val credential=AndroidRememberedLoginStore(context).load()
        val allowed=AndroidQolStore(context).load().mailboxStorageAllowed
        maintainPeriodic(context,periodicEligible(config,credential,allowed))
    }catch(_:Exception){
        try{maintainPeriodic(context,false)}catch(_:Exception){}
        false
    }
    fun valid(data:Map<String,String>)=data==mapOf("event" to "peerchat_pending","version" to "1")
    @Synchronized fun schedule(context:Context,data:Map<String,String>):Boolean {
        if(!valid(data))return false
        val scheduler=context.getSystemService(JobScheduler::class.java)
        val pending=scheduler.getPendingJob(JOB_ID)
        if(equivalentImmediate(running.contains(JOB_ID),pending!=null,pending?.minLatencyMillis ?: 0L,pending!=null&&delayed(scheduler))){
            android.util.Log.i("PeerChatWake","stage=wake_coalesced")
            return true
        }
        val builder=JobInfo.Builder(JOB_ID,ComponentName(context,PeerChatWakeJob::class.java))
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
        if(Build.VERSION.SDK_INT>=31)builder.setExpedited(true) else builder.setOverrideDeadline(30000)
        if(scheduler.schedule(builder.build())==JobScheduler.RESULT_SUCCESS){
            android.util.Log.i("PeerChatWake","stage=wake_schedule_accepted expedited=${Build.VERSION.SDK_INT>=31}")
            return true
        }
        android.util.Log.i("PeerChatWake","stage=wake_expedited_rejected")
        // Quota exhaustion is not permission to hold an immortal socket.
        val ordinary=JobInfo.Builder(JOB_ID,ComponentName(context,PeerChatWakeJob::class.java))
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setOverrideDeadline(30000).build()
        return scheduler.schedule(ordinary)==JobScheduler.RESULT_SUCCESS
    }
}
