package com.peerchat.android.platform

import android.Manifest
import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.media.AudioAttributes
import android.net.Uri
import com.peerchat.android.MainActivity
import com.peerchat.android.R
import com.peerchat.android.application.VoiceCallTarget
import com.peerchat.android.application.VoiceSnapshot

/** Incoming signaling is not microphone consent. Notification only opens the
 * visible application; Answer there performs permission and exact-call checks. */
internal class VoiceNotifications(private val context:Context) {
    private var shown:VoiceCallTarget?=null
    fun update(voice:VoiceSnapshot) {
        val incoming=voice.target?.takeIf{voice.direction==1L&&voice.state==2L}
        if(incoming==shown)return
        try {
            val manager=context.getSystemService(NotificationManager::class.java)
            if(incoming==null){manager.cancel(ID);shown=null;return}
            if(Build.VERSION.SDK_INT>=33&&context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)return
            val previousChannel=manager.getNotificationChannel("peerchat_incoming_voice")
            val channel=NotificationChannel(CHANNEL,"Incoming voice calls",previousChannel?.importance?:NotificationManager.IMPORTANCE_HIGH)
            previousChannel?.let{channel.enableVibration(it.shouldVibrate())}
            channel.setSound(Uri.parse("android.resource://${context.packageName}/${R.raw.eminem_ringtone}"),AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
            manager.createNotificationChannel(channel)
            val open=PendingIntent.getActivity(context,5,Intent(context,MainActivity::class.java),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            manager.notify(ID,Notification.Builder(context,CHANNEL).setSmallIcon(R.drawable.peerchat_icon)
                .setContentTitle("Incoming PeerChat voice call").setContentText("Open PeerChat to answer or decline")
                .setCategory(Notification.CATEGORY_CALL).setVisibility(Notification.VISIBILITY_PRIVATE)
                .setContentIntent(open).setAutoCancel(false).setOngoing(true).setTimeoutAfter(30_000)
                .addAction(Notification.Action.Builder(null,"Open call",open).build()).build())
            shown=incoming
        }catch(_:RuntimeException){/* Foreground call controls remain available. */}
    }
    private companion object {const val CHANNEL="peerchat_incoming_voice_eminem_v1";const val ID=31004}
}
