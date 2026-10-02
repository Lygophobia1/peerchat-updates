package com.peerchat.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.viewmodel.compose.viewModel
import com.peerchat.android.ui.PeerChatApp
import com.peerchat.android.ui.PeerChatUiModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            // UI-only shell: never initializes native handles, Keystore or account services.
            val model: PeerChatUiModel = viewModel()
            PeerChatApp(model)
        }
    }
}
