package com.peerchat.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.Modifier
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
@Config(sdk=[34],qualifiers="w360dp-h800dp-port")
class ArchiveDevicesUiTest {
    @get:Rule val compose=createComposeRule()
    @Test fun issuerConsentIsNeverImpliedByRenderingOrOpeningTheDialog(){
        val id="a".repeat(64)
        val context=ArchiveEnrollmentSnapshot(8,3,id,listOf(ArchiveDeviceSnapshot(id,true,false,false,false)))
        val state=PeerChatUiState(demo=false,account=AccountUi("8","Test"),archiveEnrollment=context)
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){Column(Modifier.verticalScroll(rememberScrollState())){ArchiveDevicesSection(state,actions::add)}}}
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Trust archive authorizer").performScrollTo().performClick()
        compose.onNodeWithText("Confirm archive authorization").assertExists()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Cancel").performClick()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Trust archive authorizer").performScrollTo().performClick()
        compose.onNodeWithText("Confirm",substring=false).performClick()
        compose.runOnIdle{
            assertEquals(1,actions.size)
            assertEquals(UiAction.ArchiveConsent(context,ArchiveDeviceAction.TrustAuthorizer,id),actions.single())
        }
    }
}
