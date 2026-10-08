package com.peerchat.android.ui

import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.*
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w360dp-h800dp-port")
class AccountNavigationUiTest {
    @get:Rule val compose=createComposeRule()
    @Test fun signInHasCreateAccountAndBackPreservesServerTrust() {
        val server=ServerUi("server.example","7443","a".repeat(64))
        var state by mutableStateOf(PeerChatUiState(route=Route.Login,server=server))
        compose.setContent{PeerChatTheme(true){PeerChatShell(state,{state=reduceUi(state,it)})}}
        compose.onNodeWithTag("account-mode-switch").performScrollTo().assertIsEnabled().performClick()
        compose.runOnIdle{assertEquals(Route.Register,state.route);assertEquals(server,state.server)}
        compose.onNodeWithText("Create your account").assertExists()
        compose.onNodeWithTag("account-mode-switch").performScrollTo().performClick()
        compose.runOnIdle{assertEquals(Route.Login,state.route);assertEquals(server,state.server)}
    }
}
