package com.peerchat.android.application

import android.net.Uri
import com.peerchat.android.platform.AttachmentSendMode

/** Local presentation hook only. Called after durable admission and before
 * publishing its snapshot; never changes authorization or delivery semantics. */
internal interface AttachmentAdmissionObserver {
    suspend fun sendAttachmentObserved(peerId:String,uri:Uri,mode:AttachmentSendMode,onQueued:(String)->Unit):String
    suspend fun sendGroupAttachmentObserved(groupId:String,uri:Uri,onQueued:(String)->Unit):String
    suspend fun resolveAttachmentCompatibilityObserved(token:String,mode:AttachmentSendMode?,onQueued:(String)->Unit):String?
}
