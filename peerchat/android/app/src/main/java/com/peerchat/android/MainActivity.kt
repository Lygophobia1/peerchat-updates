package com.peerchat.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                var status by remember { mutableStateOf("Checking native foundation…") }
                LaunchedEffect(Unit) {
                    status = withContext(Dispatchers.Default) {
                        try { NativeBridge.prove() + "\n" + com.peerchat.android.platform.SecurityProof.run(applicationContext) }
                        catch (failure: Throwable) { "Native proof failed: ${failure.javaClass.simpleName}" }
                    }
                }
                androidx.compose.material3.Surface(modifier = Modifier.fillMaxSize()) {
                    Column(Modifier.padding(24.dp)) {
                        Text("PeerChat Android dev.1", style = MaterialTheme.typography.headlineSmall)
                        Text(status, modifier = Modifier.padding(top = 16.dp))
                        Text("Messaging portability proof only — no account, messages or media yet.", modifier = Modifier.padding(top = 16.dp))
                    }
                }
            }
        }
    }
}
