package com.peerchat.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.viewmodel.compose.viewModel
import com.peerchat.android.ui.PeerChatApp
import com.peerchat.android.ui.PeerChatUiModel
import com.peerchat.android.ui.NativePresenter

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            if(BuildConfig.DEBUG && intent.getBooleanExtra("peerchat_ui_demo",false)) {
                val model: PeerChatUiModel = viewModel()
                PeerChatApp(model)
            } else {
                val model: NativePresenter = viewModel()
                PeerChatApp(model)
            }
        }
    }
}
