package com.peerchat.android.platform

import android.app.Application
import com.google.firebase.FirebaseApp
import com.google.firebase.messaging.FirebaseMessaging
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import com.peerchat.android.BuildConfig

/** Vendor adapter only: no authentication, sockets, history or message transport. */
class PeerChatAndroidApplication:Application() {
    override fun onCreate() {
        super.onCreate()
        if(!BuildConfig.FCM_CONFIGURED)return
        if(FirebaseApp.getApps(this).isEmpty() && FirebaseApp.initializeApp(this)==null)return
        val messaging=FirebaseMessaging.getInstance()
        messaging.isAutoInitEnabled=true
        messaging.token.addOnSuccessListener { token ->
            try {AndroidPushRegistration(this).replaceFcmToken(token)}catch(_:Exception){/* Never log vendor tokens or provider messages. */}
        }
    }
}
class PeerChatFcmService:FirebaseMessagingService() {
    override fun onNewToken(token:String) {
        if(!BuildConfig.FCM_CONFIGURED)return
        try{AndroidPushRegistration(this).replaceFcmToken(token)}catch(_:Exception){}
        // Existing foreground refresh picks up replacement. Background work uses
        // the same native/application lease and cannot authenticate from a token.
        OpaquePushWake.schedule(this,mapOf("event" to "peerchat_pending","version" to "1"))
    }
    override fun onMessageReceived(message:RemoteMessage) {
        if(BuildConfig.FCM_CONFIGURED && message.notification==null && OpaquePushWake.valid(message.data)) {
            // Priority enums and time only: never FCM IDs, token or payload contents.
            android.util.Log.i("PeerChatWake","stage=fcm_received utc_ms=${System.currentTimeMillis()} original_priority=${message.originalPriority} effective_priority=${message.priority}")
            val scheduled=OpaquePushWake.schedule(this,message.data)
            android.util.Log.i("PeerChatWake",if(scheduled)"stage=wake_scheduled" else "stage=wake_schedule_unavailable")
            // Schedule first to preserve the immediate high-priority FCM window.
            WakeSystemDiagnostics.power(this,"fcm_power")
        }
    }
}
