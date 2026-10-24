package com.peerchat.android.platform

import android.Manifest
import android.app.*
import android.content.*
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.*
import com.peerchat.android.MainActivity
import com.peerchat.android.R
import com.peerchat.android.application.*
import com.peerchat.android.platform.audio.AndroidCallAudio
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.distinctUntilChanged

/** Explicit-call platform lifetime only. Native CallManager/VoiceMedia own all
 * signaling, media and recovery. No persisted intent or automatic microphone. */
class PeerChatVoiceService:Service() {
    private val scope=CoroutineScope(SupervisorJob()+Dispatchers.Main.immediate)
    private var lease:MessagingApplication?=null
    private var audio:AndroidCallAudio?=null
    private var token:String?=null
    private var target:VoiceCallTarget?=null
    private var work:Job?=null
    private var stopping=false
    override fun onBind(intent:Intent?):IBinder?=null
    override fun onStartCommand(intent:Intent?,flags:Int,startId:Int):Int {
        when(intent?.action){
            END->{if(intent.getStringExtra(TOKEN)==token&&token!=null)finish() else if(token==null)stopSelf(startId);return START_NOT_STICKY}
            ROUTE->{
                val current=target
                if(current!=null&&intent.getStringExtra("owner")==current.owner&&intent.getStringExtra("peer")==current.peer&&
                    intent.getStringExtra("call")==current.id&&intent.getLongExtra("lifetime",0)==current.lifetime&&
                    intent.getStringExtra("group").orEmpty()==current.group&&intent.getStringExtra("room").orEmpty()==current.room&&
                    intent.getStringExtra("epoch").orEmpty()==current.epoch&&intent.getStringExtra("participation").orEmpty()==current.participation&&!stopping){
                    val hasRoute=intent.hasExtra("deviceId")
                    val deviceId=intent.getIntExtra("deviceId",-1)
                    val enabled=intent.getBooleanExtra("speaker",false)
                    val accepted=if(hasRoute)audio?.setRoute(deviceId)==true else audio?.setSpeaker(enabled)==true
                    val speaker=if(hasRoute)audio?.speakerActive()==true else enabled
                    scope.launch{runCatching{lease?.voicePlatformState(checkNotNull(token),if(accepted)"" else "Audio route unavailable",if(accepted)speaker else lease?.state?.value?.voice?.speaker==true)}}
                }else if(token==null)stopSelf(startId)
                return START_NOT_STICKY
            }
            START->Unit
            else->{if(token==null)stopSelf(startId);return START_NOT_STICKY}
        }
        val requested=intent.getStringExtra(TOKEN)
        if(requested==null||!requested.matches(Regex("[0-9a-f-]{36}"))){if(token==null)stopSelf(startId);return START_NOT_STICKY}
        if(token!=null){
            // Duplicate service start cannot recreate audio or extend consent.
            if(requested!=token)scope.launch{runCatching{lease?.releaseVoice(requested)}}
            return START_NOT_STICKY
        }
        try {
            check(checkSelfPermission(Manifest.permission.RECORD_AUDIO)==PackageManager.PERMISSION_GRANTED)
            token=requested
            val manager=getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(NotificationChannel(CHANNEL,"Voice calls",NotificationManager.IMPORTANCE_LOW))
            val note=notification("Preparing call")
            if(Build.VERSION.SDK_INT>=30)startForeground(NOTIFICATION,note,ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
            else startForeground(NOTIFICATION,note)
            // Android 15 requires a top app or foreground service for focus.
            work=scope.launch {
                var acquired:MessagingApplication?=null
                var preparationStage="runtime_lease"
                try {
                    // Runtime acquisition may briefly await native ownership;
                    // never block the main looper or foreground-start deadline.
                    withContext(Dispatchers.IO){acquired=AndroidMessagingRuntime.acquireVoice(this@PeerChatVoiceService,requested)}
                    ensureActive();lease=acquired
                    android.util.Log.i("PeerChatVoice","stage=runtime_lease result=ready")
                    preparationStage="audio_prepare"
                    audio=AndroidCallAudio(this@PeerChatVoiceService){finish("Audio focus or device unavailable")}
                    val automaticNoiseControl=getSharedPreferences("voice-processing",MODE_PRIVATE).getBoolean("automatic-noise-control",true)
                    check(audio!!.prepare(AndroidCallAudio.Processing(echoCancellation=true,automaticNoiseControl=automaticNoiseControl,automaticNoiseControlLevel=getSharedPreferences("voice-processing",MODE_PRIVATE).getInt("automatic-noise-level",1).coerceIn(0,2)))){"Audio focus or device unavailable"}
                    android.util.Log.i("PeerChatVoice","stage=audio_prepare result=ready")
                    preparationStage="activate"
                    val started=withTimeout(30_000){checkNotNull(lease).activateVoice(requested)}
                    target=checkNotNull(started.target)
                    android.util.Log.i("PeerChatVoice","stage=activate result=ready")
                    // Counter updates must not repeatedly republish the OS notification.
                    checkNotNull(lease).state.map{Triple(it.authenticated,it.voice.target,it.voice.diagnostic)}.distinctUntilChanged().collectLatest { (authenticated,current,status)->
                        if(!authenticated||current!=target){
                            android.util.Log.w("PeerChatVoice","stage=owner_watch result=call_target_changed")
                            finish(if(!authenticated)"owner_changed" else "call_target_changed");return@collectLatest
                        }
                        manager.notify(NOTIFICATION,notification(status))
                    }
                }catch(failure:CancellationException){
                    android.util.Log.w("PeerChatVoice","stage=$preparationStage result="+failure.javaClass.simpleName)
                    if(!stopping)finish("Call preparation failed")
                }catch(failure:Exception){
                    android.util.Log.w("PeerChatVoice","stage=$preparationStage result="+failure.javaClass.simpleName)
                    finish("Call preparation failed")
                }
                finally{if(lease!==acquired)acquired?.close()}
            }
        }catch(_:Exception){finish("Audio focus or device unavailable")}
        return START_NOT_STICKY
    }
    private fun finish(diagnostic:String="") {
        if(stopping)return
        val reason=when(diagnostic){
            ""->"call_target_changed"
            "Audio focus or device unavailable"->"audio_focus_or_device_unavailable"
            "Call preparation failed"->"call_preparation_failed"
            "owner_changed"->"owner_changed"
            "call_target_changed"->"call_target_changed"
            else->"explicit_finish"
        }
        android.util.Log.i("PeerChatVoice","stage=finish reason=$reason")
        stopping=true
        // Stop capture immediately; deferred cleanup cannot operate a newer call.
        audio?.close();audio=null
        work?.cancel()
        val owner=lease;val expected=token
        scope.launch {
            try{if(owner!=null&&expected!=null){
                if(diagnostic.isNotEmpty()&&diagnostic!="owner_changed"&&diagnostic!="call_target_changed")runCatching{owner.voicePlatformState(expected,diagnostic)}
                runCatching{owner.releaseVoice(expected)}
            }}finally{stopSelf()}
        }
    }
    private fun notification(status:String):Notification {
        val open=PendingIntent.getActivity(this,3,Intent(this,MainActivity::class.java),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        // Unique data prevents an old notification action from being rebound to
        // a replacement call by PendingIntent UPDATE_CURRENT.
        val endIntent=Intent(this,PeerChatVoiceService::class.java).setAction(END)
            .setData(android.net.Uri.parse("peerchat-call:"+token)).putExtra(TOKEN,token)
        val end=PendingIntent.getService(this,4,endIntent,PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        return Notification.Builder(this,CHANNEL).setSmallIcon(R.drawable.peerchat_icon).setContentTitle("PeerChat voice call")
            .setContentText(status.take(160)).setCategory(Notification.CATEGORY_CALL).setContentIntent(open)
            .setOngoing(true).setOnlyAlertOnce(true).setVisibility(Notification.VISIBILITY_PRIVATE)
            .addAction(Notification.Action.Builder(null,"Hang up",end).build()).build()
    }
    override fun onDestroy(){
        stopping=true;audio?.close();audio=null;work?.cancel();scope.cancel()
        val owner=lease;val expected=token;lease=null;token=null;target=null
        // Do not cancel this exact-owner cleanup with the destroyed service job.
        if(owner!=null)CoroutineScope(Dispatchers.IO).launch {
            try{if(expected!=null)runCatching{owner.releaseVoice(expected)}}finally{owner.close()}
        }
        stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy()
    }
    companion object {
        private const val CHANNEL="peerchat_active_voice"
        private const val NOTIFICATION=31003
        private const val TOKEN="voice_consent"
        private const val START="com.peerchat.android.START_EXPLICIT_VOICE"
        private const val END="com.peerchat.android.END_EXPLICIT_VOICE"
        private const val ROUTE="com.peerchat.android.VOICE_AUDIO_ROUTE"
        /** Caller must be a visible activity handling its explicit consent. */
        fun start(context:Context,token:String):Boolean=try {
            context.startForegroundService(Intent(context,PeerChatVoiceService::class.java).setAction(START).putExtra(TOKEN,token));true
        }catch(_:RuntimeException){false}
        fun route(context:Context,target:VoiceCallTarget,enabled:Boolean):Boolean=try {
            context.startService(Intent(context,PeerChatVoiceService::class.java).setAction(ROUTE)
                .putExtra("owner",target.owner).putExtra("peer",target.peer).putExtra("call",target.id)
                .putExtra("lifetime",target.lifetime).putExtra("group",target.group).putExtra("room",target.room)
                .putExtra("epoch",target.epoch).putExtra("participation",target.participation).putExtra("speaker",enabled));true
        }catch(_:RuntimeException){false}
        fun route(context:Context,target:VoiceCallTarget,deviceId:Int):Boolean=try {
            context.startService(Intent(context,PeerChatVoiceService::class.java).setAction(ROUTE)
                .putExtra("owner",target.owner).putExtra("peer",target.peer).putExtra("call",target.id)
                .putExtra("lifetime",target.lifetime).putExtra("group",target.group).putExtra("room",target.room)
                .putExtra("epoch",target.epoch).putExtra("participation",target.participation).putExtra("deviceId",deviceId));true
        }catch(_:RuntimeException){false}
        fun cancel(context:Context,token:String):Boolean=try {
            context.startService(Intent(context,PeerChatVoiceService::class.java).setAction(END)
                .setData(android.net.Uri.parse("peerchat-call:$token")).putExtra(TOKEN,token));true
        }catch(_:RuntimeException){false}
    }
}
