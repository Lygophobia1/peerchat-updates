package com.peerchat.android.application

import com.peerchat.android.platform.AccountSession
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.flow.StateFlow

/** UI/application seam only. Implementations expose owner snapshots and commands,
 * not sockets, protocol frames, mutable message queues or private keys. */
interface MessagingApplication:AutoCloseable {
    val state:StateFlow<MessagingSnapshot>
    suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String):AccountSession
    suspend fun send(peerId:String,text:String):String
    suspend fun older(peerId:String,before:Long):List<MessageSnapshot>
    suspend fun requestFriend(username:String)
    suspend fun respondFriend(accountId:Long,accept:Boolean)
    suspend fun signOut()
}
