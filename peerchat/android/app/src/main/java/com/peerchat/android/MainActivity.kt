package com.peerchat.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.runtime.SideEffect
import android.content.Intent
import androidx.lifecycle.Lifecycle
import com.peerchat.android.ui.PeerChatApp
import com.peerchat.android.ui.PeerChatUiModel
import com.peerchat.android.ui.NativePresenter

class MainActivity : ComponentActivity() {
    private var nativePresenter:NativePresenter?=null
    private var pendingNotification:Pair<String,String>?=null
    private fun notificationIntent(intent:Intent){
        val key=intent.getStringExtra("peerchat_account_key") ?: return
        val conversation=intent.getStringExtra("peerchat_conversation") ?: return
        val presenter=nativePresenter
        if(presenter!=null)presenter.openNotification(key,conversation) else pendingNotification=key to conversation
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        notificationIntent(intent)
        setContent {
            if(BuildConfig.DEBUG && intent.getBooleanExtra("peerchat_ui_demo",false)) {
                val model: PeerChatUiModel = viewModel()
                PeerChatApp(model)
            } else {
                val model: NativePresenter = viewModel()
                SideEffect{
                    if(nativePresenter!==model){nativePresenter=model;model.setForeground(lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED))}
                    pendingNotification?.let{model.openNotification(it.first,it.second);pendingNotification=null}
                }
                PeerChatApp(model)
            }
        }
    }
    override fun onStart(){super.onStart();nativePresenter?.setForeground(true)}
    override fun onStop(){nativePresenter?.setForeground(false);super.onStop()}
    override fun onNewIntent(intent:Intent){super.onNewIntent(intent);setIntent(intent);notificationIntent(intent)}
}
