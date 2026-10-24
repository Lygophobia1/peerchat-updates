package com.peerchat.android.platform
import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Intent
import android.media.AudioAttributes
import android.net.Uri
import android.content.pm.PackageManager
import android.os.Build
import com.peerchat.android.MainActivity
import com.peerchat.android.R
import com.peerchat.android.application.*

internal fun showMessageNotification(context:android.content.Context,accountKey:String,value:LocalMessageNotification):Boolean {
        android.util.Log.i("PeerChatWake","stage=notification_attempt")
        if(Build.VERSION.SDK_INT>=33&&context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){
            android.util.Log.i("PeerChatWake","stage=notification_unavailable reason=permission");return false
        }
        val manager=context.getSystemService(NotificationManager::class.java);if(!manager.areNotificationsEnabled()){
            android.util.Log.i("PeerChatWake","stage=notification_unavailable reason=app_disabled");return false
        }
        val previousMessageChannel=manager.getNotificationChannel("messages")
        val messageChannel=NotificationChannel("messages_xbox_v1","Messages",previousMessageChannel?.importance?:NotificationManager.IMPORTANCE_DEFAULT)
        previousMessageChannel?.let{messageChannel.enableVibration(it.shouldVibrate())}
        messageChannel.setSound(Uri.parse("android.resource://${context.packageName}/${R.raw.xbox}"),AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_NOTIFICATION_COMMUNICATION_INSTANT).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
        manager.createNotificationChannel(messageChannel)
        val id=(accountKey+value.conversationId).hashCode()
        val intent=Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            .putExtra("peerchat_account_key",accountKey).putExtra("peerchat_conversation",value.conversationId)
        val pending=PendingIntent.getActivity(context,id,intent,PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val notification=Notification.Builder(context,"messages_xbox_v1").setSmallIcon(R.drawable.peerchat_icon)
            .setContentTitle(value.title).setContentText(value.body).setContentIntent(pending).setAutoCancel(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE).setNumber(value.count).setGroup("peerchat_messages").build()
        manager.notify(id,notification)
        android.util.Log.i("PeerChatWake","stage=notification_posted")
        WakeSystemDiagnostics.power(context,"notification_posted_power")
        return true
    }

/** Notification execution only; native history and protected read markers own state. */
internal class GroupNotifications(private val context:android.content.Context,profile:String) {
    private val global=AndroidQolStore(context,profile)
    private val groups=AndroidGroupQolStore(context,profile)
    fun publish(endpoint:ServerEndpoint,snapshot:MessagingSnapshot) {
        if(!snapshot.authenticated)return
        val account=accountPreferenceKey(endpoint,snapshot.accountId);val settings=global.load()
        for(group in snapshot.groups){
            val key=groupPreferenceKey(account,group.id);var prior=groups.load().conversations[key]
            val incoming=group.messages.filter{it.device!=snapshot.localPeerId}.sortedBy{it.sequence}
            val newest=incoming.lastOrNull()?.sequence?:0L
            if(prior==null||prior.baselinePending){
                if(group.messages.isEmpty()&&group.hasOlder)continue
                // Existing history is silent; only newly admitted live messages
                // contribute to this session's unread count.
                val baseline=incoming.dropLast(group.unread.coerceAtMost(incoming.size)).lastOrNull()?.sequence?:0L
                groups.update{state->
                    if(state.conversations.size>=128&&!state.conversations.containsKey(key))state else {
                        val current=state.conversations[key]?:ConversationPreference()
                        state.copy(conversations=state.conversations+(key to current.copy(readSequence=maxOf(current.readSequence,baseline),notifiedSequence=maxOf(current.notifiedSequence,baseline),baselinePending=false)))
                    }
                }
                prior=groups.load().conversations[key]?:continue
            }
            val planned=planGroupMessageNotification(snapshot,group,prior,settings.preview)
            if(settings.notifications&&planned!=null&&showMessageNotification(context,account,planned)){
                val row=prior
                groups.update{val current=it.conversations[key]?:row;it.copy(conversations=it.conversations+(key to current.copy(notifiedSequence=maxOf(current.notifiedSequence,newest))))}
            }
        }
    }
}
