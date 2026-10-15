package com.peerchat.android.application

import com.peerchat.android.platform.AccountSession
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.flow.StateFlow

/** UI/application seam only. Implementations expose owner snapshots and commands,
 * not sockets, protocol frames, mutable message queues or private keys. */
interface MessagingApplication:AutoCloseable {
    val state:StateFlow<MessagingSnapshot>
    suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String):AccountSession
    suspend fun createAccountRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession = error("Account creation unavailable")
    suspend fun signInRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession {
        check(!remember){"Remember Me is unavailable in this application binding"}
        return signIn(endpoint,username,password)
    }
    suspend fun restoreRemembered(endpoint:ServerEndpoint):Boolean = false
    suspend fun restoreLocalHistory(endpoint:ServerEndpoint):Boolean = false
    fun setForeground(active:Boolean) {}
    /** Readout only: native output was drained after received-history observation. */
    fun backgroundOutputDrained(afterNanos:Long):Boolean = false
    suspend fun awaitQuiescence(milliseconds:Long):Boolean = true
    suspend fun verifyDevice(value:DeviceVerification,accept:Boolean){error("Device verification unavailable")}
    suspend fun send(peerId:String,text:String):String
    suspend fun older(peerId:String,before:Long):List<MessageSnapshot>
    suspend fun createGroup(name:String){error("Group creation unavailable")}
    suspend fun inviteGroup(id:String,target:Long){error("Group invitation unavailable")}
    suspend fun respondGroup(id:String,accept:Boolean){error("Group invitation unavailable")}
    suspend fun sendGroup(id:String,text:String):String=error("Group send unavailable")
    suspend fun retryGroup(id:String,message:String){error("Group retry unavailable")}
    suspend fun groupHistory(id:String,before:Long=Long.MAX_VALUE):List<GroupMessageSnapshot> = error("Group history unavailable")
    suspend fun sendAttachment(peerId:String,uri:android.net.Uri):String = error("File sharing unavailable")
    suspend fun attachmentAction(id:String,accept:Boolean) { error("File sharing unavailable") }
    suspend fun attachmentText(id:String):String = error("Text preview unavailable")
    suspend fun requestFriend(username:String)
    suspend fun respondFriend(accountId:Long,accept:Boolean)
    suspend fun signOut()
    /** Recovery only: retain protected credentials, Profile, keys and history. */
    suspend fun resetNetworkSession() { error("Network recovery unavailable") }
    suspend fun setMailboxStorageAllowed(allowed:Boolean){check(!allowed){"Mailbox configuration unavailable"}}
    suspend fun archiveDevices():ArchiveEnrollmentSnapshot = error("Archive enrollment unavailable")
    suspend fun synchronizeArchive(snapshot:ArchiveEnrollmentSnapshot,source:String?,pause:Boolean):ArchiveEnrollmentSnapshot = error("Archive synchronization unavailable")
    suspend fun setHistorySync(snapshot:ArchiveEnrollmentSnapshot,enabled:Boolean):ArchiveEnrollmentSnapshot = error("History policy unavailable")
    suspend fun archiveConsent(snapshot:ArchiveEnrollmentSnapshot,action:ArchiveDeviceAction,device:String,read:Boolean,serve:Boolean):ArchiveEnrollmentSnapshot = error("Archive enrollment unavailable")
}
