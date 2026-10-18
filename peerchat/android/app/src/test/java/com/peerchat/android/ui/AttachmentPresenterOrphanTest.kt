package com.peerchat.android.ui

import android.app.Application
import android.net.Uri
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.application.AttachmentCompatibilityChoice
import com.peerchat.android.application.AttachmentCompatibilityRequired
import com.peerchat.android.application.AttachmentSelectionOwner
import com.peerchat.android.application.ConnectionSnapshot
import com.peerchat.android.application.ConversationSnapshot
import com.peerchat.android.application.FriendIdentity
import com.peerchat.android.application.MessagingApplication
import com.peerchat.android.application.MessagingSnapshot
import com.peerchat.android.application.MessageSnapshot
import com.peerchat.android.platform.AccountSession
import com.peerchat.android.platform.AndroidQolRepository
import com.peerchat.android.platform.AndroidQolState
import com.peerchat.android.platform.ServerConfigurationRepository
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.setMain
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Reproduces loss of a process-local choice when the UI owner is replaced. */
@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class AttachmentPresenterOrphanTest {
    private class RetainedBackend:MessagingApplication {
        override val state=MutableStateFlow(MessagingSnapshot(19,"Jonah","a".repeat(64),true,"Authenticated",
            listOf(ConversationSnapshot(FriendIdentity(8,"Sammy","b".repeat(64),true),ConnectionSnapshot(ready=true),emptyList())),
            serverPresenceLive=true,localHistoryReady=true))
        private val selection=AttachmentSelectionOwner<AttachmentCompatibilityChoice>()
        val pending get()=selection.pending
        var attempts=0
        var cancellations=0
        override suspend fun sendAttachment(peerId:String,uri:Uri):String {
            val (intent,_)=selection.begin {check(peerId=="b".repeat(64))}
            attempts++
            val choice=AttachmentCompatibilityChoice(attempts.toString().padStart(32,'f'),"selected.bin",true,peerId)
            selection.pending=choice
            selection.finish(intent)
            throw AttachmentCompatibilityRequired(choice)
        }
        override suspend fun resolveAttachmentCompatibility(token:String,direct:Boolean):String? {
            if(!direct&&pending?.token==token){selection.clearIfCurrent(checkNotNull(pending));cancellations++}
            return null
        }
        override suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String)=AccountSession(19,"Jonah","fixture",true)
        override suspend fun send(peerId:String,text:String)="c".repeat(32)
        override suspend fun older(peerId:String,before:Long)=emptyList<MessageSnapshot>()
        override suspend fun requestFriend(username:String){}
        override suspend fun respondFriend(accountId:Long,accept:Boolean){}
        override suspend fun signOut(){}
        override fun close(){} // Runtime closes a lease, retaining its backend.
    }
    private class Settings:ServerConfigurationRepository {
        override fun load():ServerEndpoint?=null
        override fun save(endpoint:ServerEndpoint){}
    }
    private class Preferences:AndroidQolRepository {
        private var saved=AndroidQolState()
        override fun load()=saved
        override fun save(value:AndroidQolState){saved=value}
    }
    @Test fun replacingPresenterCanSupersedeOrphanedCompatibilityChoice(){
        val dispatcher=StandardTestDispatcher()
        Dispatchers.setMain(dispatcher)
        val backend=RetainedBackend()
        val firstStore=ViewModelStore()
        val secondStore=ViewModelStore()
        try {
            fun presenter()=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,
                Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            val first=presenter()
            firstStore.put("first",first)
            dispatcher.scheduler.runCurrent()
            first.dispatch(UiAction.Select("account:8"))
            first.dispatch(UiAction.SendAttachment(Uri.parse("content://fixture/selected")))
            dispatcher.scheduler.runCurrent()
            val firstChoice=checkNotNull(first.state.value.attachmentCompatibility)
            assertNotNull(backend.pending)

            firstStore.clear() // A replacement UI has no token from the old dialog.
            dispatcher.scheduler.runCurrent()
            assertEquals(0,backend.cancellations)

            val second=presenter()
            secondStore.put("second",second)
            dispatcher.scheduler.runCurrent()
            assertNull(second.state.value.attachmentCompatibility)
            second.dispatch(UiAction.Select("account:8"))
            second.dispatch(UiAction.SendAttachment(Uri.parse("content://fixture/next")))
            dispatcher.scheduler.runCurrent()
            assertEquals(2,backend.attempts)
            val secondChoice=checkNotNull(second.state.value.attachmentCompatibility)
            assertEquals(secondChoice,backend.pending)
            assertNotEquals(firstChoice.token,secondChoice.token)
            kotlinx.coroutines.runBlocking{backend.resolveAttachmentCompatibility(firstChoice.token,false)}
            assertEquals(secondChoice,backend.pending)
            assertEquals(0,backend.cancellations)
        }finally{firstStore.clear();secondStore.clear();Dispatchers.resetMain()}
    }
}
