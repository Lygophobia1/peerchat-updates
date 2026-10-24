package com.peerchat.android.application

import com.peerchat.android.platform.AccountSession
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.AttachmentSendMode
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
    suspend fun reenrollMailbox(value:MailboxReenrollment,accept:Boolean){error("Mailbox re-enrollment unavailable")}
    suspend fun send(peerId:String,text:String):String
    suspend fun sendToAccount(localAccount:Long,recipientAccount:Long,text:String):String {
        val current=state.value
        check(localAccount>0&&current.accountId==localAccount)
        return send(current.conversations.single{it.friend.accountId==recipientAccount}.friend.peerId,text)
    }
    suspend fun prepareVoice(peerId:String,answer:VoiceCallTarget?=null):VoiceConsent = error("Voice unavailable")
    suspend fun prepareGroupVoice(group:String,room:String):VoiceConsent = error("Group voice unavailable")
    suspend fun activateVoice(token:String):VoiceSnapshot = error("Voice unavailable")
    suspend fun releaseVoice(token:String) {}
    suspend fun controlVoice(target:VoiceCallTarget,action:VoiceControl,value:Boolean=false) { error("Voice unavailable") }
    suspend fun voicePlatformState(token:String,diagnostic:String,speaker:Boolean=false) {}
    suspend fun deleteLocalMessage(localAccount:Long,conversation:String,messageId:String,transferId:String?=null) { error("Local deletion unavailable") }
    suspend fun deleteLocalConversation(localAccount:Long,conversation:String) { error("Local deletion unavailable") }
    suspend fun older(peerId:String,before:Long):List<MessageSnapshot>
    suspend fun createGroup(name:String){error("Group creation unavailable")}
    suspend fun inviteGroup(id:String,target:Long){error("Group invitation unavailable")}
    suspend fun respondGroup(id:String,accept:Boolean){error("Group invitation unavailable")}
    suspend fun sendGroup(id:String,text:String):String=error("Group send unavailable")
    suspend fun retryGroup(id:String,message:String){error("Group retry unavailable")}
    suspend fun groupHistory(id:String,before:Long=Long.MAX_VALUE):List<GroupMessageSnapshot> = error("Group history unavailable")
    suspend fun sendGroupAttachment(groupId:String,uri:android.net.Uri):String = error("Group file sharing unavailable")
    suspend fun sendAttachment(peerId:String,uri:android.net.Uri,mode:AttachmentSendMode):String = error("File sharing unavailable")
    suspend fun resolveAttachmentCompatibility(token:String,mode:AttachmentSendMode?):String? = error("File sharing unavailable")
    suspend fun activeAttachmentTransfers(ids:Set<String>):Set<String> = emptySet()
    suspend fun attachmentAction(id:String,accept:Boolean) { error("File sharing unavailable") }
    suspend fun attachmentText(id:String):String = error("Text preview unavailable")
    suspend fun requestFriend(username:String)
    suspend fun searchPeople(query:String):List<DirectoryAccountSnapshot> = error("People search unavailable")
    suspend fun cancelFriend(accountId:Long) { error("Request cancellation unavailable") }
    suspend fun refreshContacts() { error("Contact refresh unavailable") }
    suspend fun selectIdentityAuthority(confirmedFingerprint:String) { error("Identity authority unavailable") }
    suspend fun endorseOwnDevice(targetDevice:String,confirmedFingerprint:String) { error("Identity endorsement unavailable") }
    suspend fun verifyContactAuthority(account:Long,confirmedFingerprint:String) { error("Contact identity verification unavailable") }
    suspend fun transferSettings():TransferSettingsSnapshot = error("Transfer settings unavailable")
    suspend fun saveTransferSettings(value:TransferSettingsSnapshot) { error("Transfer settings unavailable") }
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
