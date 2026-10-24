package com.peerchat.android.ui

import android.app.Application
import android.net.Uri
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.application.*
import com.peerchat.android.platform.*
import kotlinx.coroutines.CompletableDeferred
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

@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class ImmediateAttachmentPresenterTest {
    private val peer="b".repeat(64)
    private val nativeId="c".repeat(32)
    private class Settings:ServerConfigurationRepository {
        override fun load():ServerEndpoint?=null
        override fun save(endpoint:ServerEndpoint){}
    }
    private class Preferences:AndroidQolRepository {
        private var saved=AndroidQolState()
        override fun load()=saved
        override fun save(value:AndroidQolState){saved=value}
    }
    private open inner class DelayedBackend:MessagingApplication {
        override val state=MutableStateFlow(MessagingSnapshot(19,"Jonah","a".repeat(64),true,"Authenticated",
            listOf(ConversationSnapshot(FriendIdentity(8,"Sammy",peer,true),ConnectionSnapshot(ready=true),emptyList())),
            serverPresenceLive=true,localHistoryReady=true))
        var result=CompletableDeferred<String>()
        val messageDeletions=mutableListOf<Pair<String,String>>()
        val conversationDeletions=mutableListOf<String>()
        override suspend fun sendAttachment(peerId:String,uri:Uri,mode:AttachmentSendMode):String=result.await()
        override suspend fun sendGroupAttachment(groupId:String,uri:Uri):String=result.await()
        override suspend fun deleteLocalMessage(localAccount:Long,conversation:String,messageId:String,transferId:String?){
            check(localAccount==19L);if(transferId!=null)check(transferId==messageId)
            messageDeletions+=conversation to messageId
        }
        override suspend fun deleteLocalConversation(localAccount:Long,conversation:String){check(localAccount==19L);conversationDeletions+=conversation}
        override suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String)=AccountSession(19,"Jonah","fixture",true)
        override suspend fun send(peerId:String,text:String)="d".repeat(32)
        override suspend fun older(peerId:String,before:Long)=emptyList<MessageSnapshot>()
        override suspend fun requestFriend(username:String){}
        override suspend fun respondFriend(accountId:Long,accept:Boolean){}
        override suspend fun signOut(){}
        override fun close(){}
    }
    private inner class ObservedBackend:DelayedBackend(),AttachmentAdmissionObserver {
        override suspend fun sendAttachmentObserved(peerId:String,uri:Uri,mode:AttachmentSendMode,onQueued:(String)->Unit):String {
            onQueued(nativeId)
            val actual=AttachmentSnapshot(nativeId,peer,"photo.jpg",123,0,true,0,System.currentTimeMillis(),"","")
            state.value=state.value.copy(conversations=state.value.conversations.map{it.copy(attachments=listOf(actual))})
            return result.await()
        }
        override suspend fun sendGroupAttachmentObserved(groupId:String,uri:Uri,onQueued:(String)->Unit):String=result.await()
        override suspend fun resolveAttachmentCompatibilityObserved(token:String,mode:AttachmentSendMode?,onQueued:(String)->Unit):String?=null
    }
    @Test fun bubbleAndLocalPreviewPrecedeBackendAndReconcileById(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        try{
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("account:8"))
            val uri=Uri.parse("content://picker/private-photo")
            presenter.dispatch(UiAction.SendAttachment(uri,AttachmentSendMode.Cached))
            val provisional=presenter.state.value.selected!!.attachments.single()
            assertTrue(provisional.id.startsWith("local-attachment-"))
            assertEquals(0,provisional.state)
            assertEquals(uri,presenter.state.value.localAttachmentPreviews[provisional.id])
            assertFalse(backend.result.isCompleted)
            dispatcher.scheduler.runCurrent()
            backend.result.complete(nativeId);dispatcher.scheduler.runCurrent()
            assertEquals(nativeId,presenter.state.value.selected!!.attachments.single().id)
            assertEquals(uri,presenter.state.value.localAttachmentPreviews[nativeId])
            val actual=AttachmentSnapshot(nativeId,peer,"photo.jpg",123,0,true,0,System.currentTimeMillis(),"","")
            backend.state.value=backend.state.value.copy(conversations=backend.state.value.conversations.map{it.copy(attachments=listOf(actual))})
            dispatcher.scheduler.runCurrent()
            assertEquals(listOf(nativeId),presenter.state.value.selected!!.attachments.map{it.id})
            assertEquals(uri,presenter.state.value.localAttachmentPreviews[nativeId])
            backend.state.value=backend.state.value.copy(conversations=backend.state.value.conversations.map{it.copy(attachments=listOf(actual.copy(state=4,done=123,localPath="/private/photo.jpg")))})
            dispatcher.scheduler.runCurrent()
            assertTrue(presenter.state.value.localAttachmentPreviews.isEmpty())
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun failureCancelAndAccountReplacementDoNotClaimDelivery(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        try{
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("account:8"))
            presenter.dispatch(UiAction.SendAttachment(Uri.parse("content://picker/first")))
            val token=presenter.state.value.selected!!.attachments.single().id
            presenter.dispatch(UiAction.CancelLocalAttachment(token))
            assertEquals(5,presenter.state.value.selected!!.attachments.single().state)
            dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.DismissLocalAttachment(token))
            assertTrue(presenter.state.value.selected!!.attachments.isEmpty())
            backend.result=CompletableDeferred()
            presenter.dispatch(UiAction.SendAttachment(Uri.parse("content://picker/stale")))
            assertTrue(presenter.state.value.localAttachmentPreviews.isNotEmpty())
            backend.state.value=backend.state.value.copy(accountId=20)
            dispatcher.scheduler.runCurrent()
            assertTrue(presenter.state.value.localAttachmentPreviews.isEmpty())
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun preAdmissionFailureStaysVisibleUntilExplicitRetry(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        try{
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("account:8"))
            presenter.dispatch(UiAction.SendAttachment(Uri.parse("content://picker/failed")))
            val token=presenter.state.value.selected!!.attachments.single().id
            dispatcher.scheduler.runCurrent()
            backend.result.completeExceptionally(IllegalStateException("preflight"));dispatcher.scheduler.runCurrent()
            assertEquals(token,presenter.state.value.selected!!.attachments.single().id)
            assertEquals(6,presenter.state.value.selected!!.attachments.single().state)
            backend.result=CompletableDeferred()
            presenter.dispatch(UiAction.RetryLocalAttachment(token))
            val retried=presenter.state.value.selected!!.attachments.single()
            assertNotEquals(token,retried.id)
            assertEquals(0,retried.state)
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun admittedFailureKeepsNativeIdentityAndNeverOffersAnotherLocalRetry(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        try{
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("account:8"))
            presenter.dispatch(UiAction.SendAttachment(Uri.parse("content://picker/admitted")))
            val local=presenter.state.value.selected!!.attachments.single().id
            dispatcher.scheduler.runCurrent()
            backend.result.completeExceptionally(QueuedAttachmentFailure(nativeId,IllegalStateException("after queue")))
            dispatcher.scheduler.runCurrent()
            assertEquals(nativeId,presenter.state.value.selected!!.attachments.single().id)
            assertEquals(6,presenter.state.value.selected!!.attachments.single().state)
            presenter.dispatch(UiAction.RetryLocalAttachment(local))
            assertEquals(nativeId,presenter.state.value.selected!!.attachments.single().id)
            val actual=AttachmentSnapshot(nativeId,peer,"photo.jpg",123,6,true,0,System.currentTimeMillis(),"failure","")
            backend.state.value=backend.state.value.copy(conversations=backend.state.value.conversations.map{it.copy(attachments=listOf(actual))})
            dispatcher.scheduler.runCurrent()
            assertEquals(listOf(nativeId),presenter.state.value.selected!!.attachments.map{it.id})
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun groupSendAlsoPublishesBeforeBackendReturns(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        try{
            backend.state.value=backend.state.value.copy(groups=listOf(GroupConversationSnapshot("room","legacy",1,"Friends",true,2)))
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("group:room"))
            val uri=Uri.parse("content://picker/group-photo")
            presenter.dispatch(UiAction.SendAttachment(uri,AttachmentSendMode.Cached))
            assertTrue(presenter.state.value.selected!!.attachments.single().id.startsWith("local-attachment-"))
            assertEquals(uri,presenter.state.value.localAttachmentPreviews.values.single())
            dispatcher.scheduler.runCurrent()
            backend.result.complete(nativeId);dispatcher.scheduler.runCurrent()
            assertEquals(nativeId,presenter.state.value.selected!!.attachments.single().id)
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun admissionCallbackReconcilesNativeSnapshotBeforeCommandReturn(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=ObservedBackend();val store=ViewModelStore()
        try{
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("account:8"))
            presenter.dispatch(UiAction.SendAttachment(Uri.parse("content://picker/racing")))
            assertTrue(presenter.state.value.selected!!.attachments.single().id.startsWith("local-attachment-"))
            dispatcher.scheduler.runCurrent()
            assertEquals(listOf(nativeId),presenter.state.value.selected!!.attachments.map{it.id})
            assertFalse(backend.result.isCompleted)
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun deletingPendingPhotoHidesItAndTombstonesItsLaterNativeId(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=ObservedBackend();val store=ViewModelStore()
        try{
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("account:8"))
            presenter.dispatch(UiAction.SendAttachment(Uri.parse("content://picker/delete-pending")))
            val token=presenter.state.value.selected!!.attachments.single().id
            presenter.dispatch(UiAction.DeleteLocalMessage("account:8",token,attachment=true))
            assertTrue(presenter.state.value.selected!!.attachments.isEmpty())
            dispatcher.scheduler.runCurrent() // Native admission and projection race the suspended return.
            assertTrue(presenter.state.value.selected!!.attachments.isEmpty())
            assertEquals(listOf("account:8" to nativeId),backend.messageDeletions)
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun deletingConversationKeepsChatRowAndClearsVisibleContent(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        val message=MessageSnapshot("d".repeat(32),"a".repeat(64),peer,100,1,1,0,"Private")
        val file=AttachmentSnapshot(nativeId,peer,"photo.jpg",123,4,true,123,100,"","/private/photo.jpg")
        try{
            backend.state.value=backend.state.value.copy(conversations=backend.state.value.conversations.map{it.copy(messages=listOf(message),attachments=listOf(file))})
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.DeleteLocalConversation("account:8"))
            assertEquals("account:8",presenter.state.value.conversations.single().id)
            assertTrue(presenter.state.value.conversations.single().messages.isEmpty())
            assertTrue(presenter.state.value.conversations.single().attachments.isEmpty())
            dispatcher.scheduler.runCurrent()
            assertEquals(listOf("account:8"),backend.conversationDeletions)
        }finally{store.clear();Dispatchers.resetMain()}
    }
    @Test fun legacyGroupMessageIdCanBeDeletedLocally(){
        val dispatcher=StandardTestDispatcher();Dispatchers.setMain(dispatcher)
        val backend=DelayedBackend();val store=ViewModelStore()
        val id="e".repeat(64)
        try{
            backend.state.value=backend.state.value.copy(groups=listOf(GroupConversationSnapshot("room","legacy",1,"Friends",true,2,
                messages=listOf(GroupMessageSnapshot(id,"a".repeat(64),19,100,1,1,"Queued","Old group message")))))
            val presenter=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),backend,Settings(),dispatcher,Preferences(),Preferences()){ "a".repeat(64) }
            store.put("presenter",presenter);dispatcher.scheduler.runCurrent()
            presenter.dispatch(UiAction.Select("group:room"))
            assertEquals(id,presenter.state.value.selected!!.messages.single().id)
            presenter.dispatch(UiAction.DeleteLocalMessage("group:room",id))
            assertTrue(presenter.state.value.selected!!.messages.isEmpty())
            dispatcher.scheduler.runCurrent()
            assertEquals(listOf("group:room" to id),backend.messageDeletions)
        }finally{store.clear();Dispatchers.resetMain()}
    }
}
