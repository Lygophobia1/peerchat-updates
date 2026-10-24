package com.peerchat.android.platform

import android.app.*
import android.content.*
import android.content.pm.ServiceInfo
import android.os.*
import com.peerchat.android.MainActivity
import com.peerchat.android.R
import com.peerchat.android.application.*
import kotlinx.coroutines.*

/** One finite platform lifetime for explicit sends/accepts. All transfer, route,
 * integrity, journal and retry decisions remain in the existing native owner. */
class PeerChatTransferService:Service() {
    private val scope=CoroutineScope(SupervisorJob()+Dispatchers.Main.immediate)
    private var lease:MessagingApplication?=null
    private var work:Job?=null
    private var lifetime:TransferLifetime?=null
    private var wake:PowerManager.WakeLock?=null
    override fun onBind(intent:Intent?):IBinder?=null
    override fun onStartCommand(intent:Intent?,flags:Int,startId:Int):Int {
        if(intent?.action==PREPARED){
            val token=intent.getStringExtra("preparation")
            if(token!=null)lifetime?.finishPreparation(token,intent.getStringExtra("transfer"),SystemClock.elapsedRealtime())
            if(lifetime?.ids.isNullOrEmpty())stopSelf()
            return START_NOT_STICKY
        }
        if(intent?.action==CANCEL){
            preparations.cancel(lifetime?.preparingIds.orEmpty())
            scope.launch {try{for(id in lifetime?.ids?.filter{it !in lifetime!!.preparingIds}.orEmpty()){
                try{lease?.attachmentAction(id,false)}catch(_:Exception){android.util.Log.w("PeerChatTransfer","native_cancel_unavailable; journal retained")}
            }}finally{stopSelf()}}
            return START_NOT_STICKY
        }
        val id=intent?.getStringExtra("transfer")
        if(id==null||!id.matches(Regex("[0-9a-f]{32}"))){if(work==null)stopSelf();return START_NOT_STICKY}
        val started=SystemClock.elapsedRealtime()
        val life=lifetime?:TransferLifetime(started).also{lifetime=it}
        if(!(if(intent?.getBooleanExtra("preparing",false)==true)life.addPreparation(id,started)else life.add(id,started)))return START_NOT_STICKY
        if(work!=null)return START_NOT_STICKY // one service/budget; no extension by coalescing
        try{
            val manager=getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(NotificationChannel(CHANNEL,"File transfers",NotificationManager.IMPORTANCE_LOW))
            val notification=notification(0,0,life.ids.size)
            if(Build.VERSION.SDK_INT>=29)startForeground(NOTIFICATION,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            else startForeground(NOTIFICATION,notification)
            lease=AndroidMessagingRuntime.acquire(this,System.nanoTime()+TransferLifetime.BUDGET_MS*1_000_000,transferLease=true)
            // Bounded, non-reference-counted CPU lease, never refreshed. No idle/
            // battery exemption. System thermal stops release it immediately.
            wake=getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"PeerChat:explicit-transfer").also{
                it.setReferenceCounted(false);it.acquire(TransferLifetime.BUDGET_MS)
            }
            work=scope.launch {
                try{withTimeout(TransferLifetime.BUDGET_MS){
                    while(isActive){
                        val power=getSystemService(PowerManager::class.java)
                        if(Build.VERSION.SDK_INT>=29&&power.currentThermalStatus>=PowerManager.THERMAL_STATUS_SEVERE){
                            android.util.Log.i("PeerChatTransfer","lifetime_stopped reason=thermal");break
                        }
                        val snapshot=checkNotNull(lease).state.value
                        val rows=(snapshot.conversations.flatMap{it.attachments}+snapshot.groups.flatMap{it.attachments}).distinctBy{it.id}.filter{it.id in life.ids}
                        val missing=life.ids-rows.map{it.id}.toSet()-life.preparingIds
                        // Local deletion hides the bubble, never the transfer's execution lifetime.
                        val hiddenActive=if(missing.isEmpty())emptySet()else try{checkNotNull(lease).activeAttachmentTransfers(missing)}catch(_:Exception){missing}
                        if(life.finished(rows,SystemClock.elapsedRealtime(),hiddenActive))break
                        val total=rows.sumOf{it.size};val done=rows.sumOf{it.done}
                        manager.notify(NOTIFICATION,notification(done,total,life.ids.size,life.preparingIds.isNotEmpty()||rows.isEmpty()||rows.all{it.awaitingUploadStart},hiddenActive.isNotEmpty()))
                        delay(1000)
                    }
                }}catch(_:CancellationException){ /* timeout/destroy retains native journal */ }
                catch(_:Exception){android.util.Log.w("PeerChatTransfer","lifetime_stopped reason=provider_failure; journal retained")}
                finally{stopSelf()}
            }
        }catch(_:Exception){android.util.Log.w("PeerChatTransfer","lifetime_start_rejected; foreground transfer remains available");stopSelf()}
        return START_NOT_STICKY
    }
    private fun notification(done:Long,total:Long,count:Int,preparing:Boolean=false,hiddenWork:Boolean=false):Notification {
        val open=PendingIntent.getActivity(this,0,Intent(this,MainActivity::class.java),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val cancel=PendingIntent.getService(this,1,Intent(this,PeerChatTransferService::class.java).setAction(CANCEL),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        return Notification.Builder(this,CHANNEL).setSmallIcon(R.drawable.peerchat_icon)
            .setContentTitle("PeerChat file transfer").setContentText(if(hiddenWork)"$count transfer(s) � Sending in background" else if(preparing)"$count transfer(s) · Preparing upload" else "$count transfer(s) · $done / $total bytes")
            .setContentIntent(open).setOngoing(true).setOnlyAlertOnce(true).setVisibility(Notification.VISIBILITY_PRIVATE)
            .setProgress(100,if(total>0)(done*100/total).coerceIn(0,100).toInt() else 0,total==0L||preparing)
            .addAction(Notification.Action.Builder(null,"Cancel transfers",cancel).build()).build()
    }
    override fun onTimeout(startId:Int,fgsType:Int){android.util.Log.i("PeerChatTransfer","lifetime_stopped reason=system_timeout");stopSelf()}
    override fun onDestroy(){
        preparations.cancel(lifetime?.preparingIds.orEmpty())
        work?.cancel();scope.cancel();lease?.close();lease=null
        wake?.let{if(it.isHeld)it.release()};wake=null
        stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy()
    }
    companion object {
        private const val CHANNEL="peerchat_explicit_transfers"
        private const val NOTIFICATION=31002
        private const val CANCEL="com.peerchat.android.CANCEL_EXPLICIT_TRANSFERS"
        private const val PREPARED="com.peerchat.android.EXPLICIT_TRANSFER_PREPARED"
        private val preparations=TransferPreparations()
        fun beginPreparation(context:Context,cancel:()->Unit):String? {
            val token=java.util.UUID.randomUUID().toString().replace("-","")
            if(!preparations.add(token,cancel))return null
            try{context.startForegroundService(Intent(context,PeerChatTransferService::class.java).putExtra("transfer",token).putExtra("preparing",true))}
            catch(_:Exception){preparations.remove(token);android.util.Log.w("PeerChatTransfer","preparation_lifetime_rejected; foreground preparation remains available");return null}
            return token
        }
        fun finishPreparation(context:Context,token:String,id:String?) {
            if(!preparations.remove(token))return
            try{context.startService(Intent(context,PeerChatTransferService::class.java).setAction(PREPARED).putExtra("preparation",token).putExtra("transfer",id))}
            catch(_:Exception){android.util.Log.w("PeerChatTransfer","preparation_handoff_unavailable; journal retained")}
        }
        fun start(context:Context,id:String){
            try{context.startForegroundService(Intent(context,PeerChatTransferService::class.java).putExtra("transfer",id))}
            catch(_:Exception){android.util.Log.w("PeerChatTransfer","lifetime_schedule_rejected; foreground transfer remains available")}
        }
    }
}
