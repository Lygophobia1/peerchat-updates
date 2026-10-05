package com.peerchat.android.platform

import android.app.job.JobScheduler
import android.app.KeyguardManager
import android.app.usage.UsageStatsManager
import android.content.Context
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log

/** Safe bounded system metadata only; never job extras, identities or messages.
 * API 36 queries use public SDK methods via reflection while compiling at API 35.
 * Observation only: no thermal override, constraint change or forced execution. */
internal object WakeSystemDiagnostics {
    fun power(context:Context,stage:String) {
        val power=context.getSystemService(PowerManager::class.java)
        val keyguard=context.getSystemService(KeyguardManager::class.java)
        val bucket=if(Build.VERSION.SDK_INT>=28)
            context.getSystemService(UsageStatsManager::class.java).appStandbyBucket else -1
        Log.i("PeerChatWake","stage=$stage utc_ms=${System.currentTimeMillis()} elapsed_ms=${SystemClock.elapsedRealtime()}"+
            " interactive=${power.isInteractive} device_idle=${power.isDeviceIdleMode}"+
            " keyguard_locked=${keyguard.isKeyguardLocked} device_locked=${keyguard.isDeviceLocked}"+
            " power_save=${power.isPowerSaveMode} standby_bucket=$bucket"+
            " battery_exempt=${power.isIgnoringBatteryOptimizations(context.packageName)}")
    }
    fun record(context:Context,stage:String,stopReason:Int?=null) {
        power(context,stage+"_power")
        val thermal=if(Build.VERSION.SDK_INT>=29)
            context.getSystemService(PowerManager::class.java).currentThermalStatus else -1
        Log.i("PeerChatWake","stage=$stage utc_ms=${System.currentTimeMillis()} elapsed_ms=${SystemClock.elapsedRealtime()} thermal=$thermal"+
            (stopReason?.let{" stop_reason=$it"} ?: ""))
        val scheduler=context.getSystemService(JobScheduler::class.java)
        try {
            if(Build.VERSION.SDK_INT>=36) {
                val reasons=JobScheduler::class.java.getMethod("getPendingJobReasons",Int::class.javaPrimitiveType)
                    .invoke(scheduler,OpaquePushWake.JOB_ID) as IntArray
                Log.i("PeerChatWake","stage=wake_pending reasons="+reasons.take(16).joinToString(","))
                val history=JobScheduler::class.java.getMethod("getPendingJobReasonsHistory",Int::class.javaPrimitiveType)
                    .invoke(scheduler,OpaquePushWake.JOB_ID) as List<*>
                val type=Class.forName("android.app.job.PendingJobReasonsInfo")
                for(row in history.takeLast(8)) {
                    val timestamp=type.getMethod("getTimestampMillis").invoke(row) as Long
                    val values=type.getMethod("getPendingJobReasons").invoke(row) as IntArray
                    Log.i("PeerChatWake","stage=wake_pending_history utc_ms=$timestamp reasons="+values.take(16).joinToString(","))
                }
            } else if(Build.VERSION.SDK_INT>=34) {
                Log.i("PeerChatWake","stage=wake_pending reason="+scheduler.getPendingJobReason(OpaquePushWake.JOB_ID))
            }
        } catch(failure:Exception) {
            Log.i("PeerChatWake","stage=wake_pending_unavailable result="+failure.javaClass.simpleName)
        }
    }
}
