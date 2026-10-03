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

/** Bounded platform execution only. No new sockets, message store, retry owner,
 * or protocol. Native delivery/ACK/dedupe run in the shared leased backend. */
class PeerChatWakeJob:JobService() {
    private val scope=CoroutineScope(SupervisorJob()+Dispatchers.IO)
    private var work:Job?=null
    @Volatile private var execution=0L
    override fun onStartJob(params:JobParameters):Boolean {
        work?.cancel()
        val admitted=++execution
        work=scope.launch {
            var lease:MessagingApplication?=null
            try{withTimeout(30000){
                val config=AndroidServerConfigurationStore(this@PeerChatWakeJob).load() ?: return@withTimeout
                val credential=AndroidRememberedLoginStore(this@PeerChatWakeJob).load() ?: return@withTimeout
                if(credential.revocationPending||credential.endpoint!=config)return@withTimeout
                lease=AndroidMessagingRuntime.acquire(this@PeerChatWakeJob)
                if(!lease!!.restoreRemembered(config))return@withTimeout
                val preferences=AndroidQolStore(this@PeerChatWakeJob)
                val accountKey=accountPreferenceKey(config,credential.accountId)
                while(isActive){
                    val snapshot=lease!!.state.value
                    if(snapshot.authenticated&&snapshot.accountId==credential.accountId){
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
                                preferences.update{state->val row=state.conversations[key] ?: prior;state.copy(conversations=state.conversations+(key to row.copy(notifiedSequence=maxOf(row.notifiedSequence,incomingMax))))}
                            }
                        }
                    }
                    delay(500)
                }
            }}catch(_:Exception){/* No request/token/content/exception-message logging. */}
            finally{lease?.close();if(execution==admitted)jobFinished(params,false)}
        };return true
    }
    private fun show(accountKey:String,value:LocalMessageNotification):Boolean {
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)return false
        val manager=getSystemService(NotificationManager::class.java);if(!manager.areNotificationsEnabled())return false
        manager.createNotificationChannel(NotificationChannel("messages","Messages",NotificationManager.IMPORTANCE_DEFAULT))
        val id=(accountKey+value.conversationId).hashCode()
        val intent=Intent(this,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            .putExtra("peerchat_account_key",accountKey).putExtra("peerchat_conversation",value.conversationId)
        val pending=PendingIntent.getActivity(this,id,intent,PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val notification=Notification.Builder(this,"messages").setSmallIcon(R.drawable.peerchat_icon)
            .setContentTitle(value.title).setContentText(value.body).setContentIntent(pending).setAutoCancel(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE).setNumber(value.count).setGroup("peerchat_messages").build()
        manager.notify(id,notification);return true
    }
    override fun onStopJob(params:JobParameters):Boolean {execution++;work?.cancel();return false}
    override fun onDestroy(){execution++;scope.cancel();super.onDestroy()}
}
