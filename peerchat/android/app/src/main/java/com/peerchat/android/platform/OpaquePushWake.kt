package com.peerchat.android.platform

import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context

/** Entry point for a future vendor SDK adapter. A hint contains no identity,
 * conversation or content. It cannot authenticate or create a notification. */
object OpaquePushWake {
    const val JOB_ID=7341
    fun valid(data:Map<String,String>)=data==mapOf("event" to "peerchat_pending","version" to "1")
    fun schedule(context:Context,data:Map<String,String>):Boolean {
        if(!valid(data))return false
        val scheduler=context.getSystemService(JobScheduler::class.java)
        if(scheduler.allPendingJobs.any{it.id==JOB_ID})return true // Coalesce repeated hints.
        val job=JobInfo.Builder(JOB_ID,ComponentName(context,PeerChatWakeJob::class.java))
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setOverrideDeadline(30000).build()
        return scheduler.schedule(job)==JobScheduler.RESULT_SUCCESS
    }
}
