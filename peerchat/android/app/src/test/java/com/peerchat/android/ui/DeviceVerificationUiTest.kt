package com.peerchat.android.ui
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class DeviceVerificationUiTest {
    @get:Rule val compose=createComposeRule()
    private val value=DeviceVerification(8,4,7,9,"Sammy","a".repeat(64),listOf("b".repeat(64)),"proof")
    @Test fun explicitActionsCarryTheExactDisplayedContactAndSession(){
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,deviceVerification=value),actions::add)}}
        compose.onNodeWithText("Sammy · account 9").assertExists()
        compose.onNodeWithText("Verify and Accept").performClick()
        compose.onNodeWithText("Keep Untrusted").performClick()
        compose.runOnIdle{assertEquals(listOf(UiAction.VerifyDevice(value,true),UiAction.VerifyDevice(value,false)),actions)}
    }
    @Test fun manyHistoricalFingerprintsKeepDecisionButtonsVisible(){
        val many=value.copy(previous=List(32){it.toString(16).padStart(64,'0')})
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,deviceVerification=many),{})}}
        compose.onNodeWithText("Verify and Accept").assertIsDisplayed()
        compose.onNodeWithText("Keep Untrusted").assertIsDisplayed()
    }
    @Test fun expiredPresentationCannotBeApproved(){
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,deviceVerification=value.copy(live=false)),{})}}
        compose.onNodeWithText("Verify and Accept").assertIsNotEnabled()
        compose.onNodeWithText("Keep Untrusted").assertIsEnabled()
    }
}
