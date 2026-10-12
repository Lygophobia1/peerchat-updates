package com.peerchat.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.Modifier
import androidx.compose.runtime.mutableStateOf
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
        compose.onNodeWithText("Trust history authorizer").assertDoesNotExist()
        compose.onNodeWithText("Manage").performScrollTo().performClick()
        compose.onNodeWithText("Advanced",substring=false).performClick()
        compose.onNodeWithText("Trust history authorizer").performClick()
        compose.onNodeWithText("Confirm history authorization").assertExists()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Cancel").performClick()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Trust history authorizer").performClick()
        compose.onNodeWithText("Confirm",substring=false).performClick()
        compose.runOnIdle{
            assertEquals(1,actions.size)
            assertEquals(UiAction.ArchiveConsent(context,ArchiveDeviceAction.TrustAuthorizer,id),actions.single())
        }
    }
    @Test fun firstUseSetupRequiresConfirmationAndUpdatesToAuthorize(){
        val local="a".repeat(64);val remote="b".repeat(64)
        val first=ArchiveDeviceSnapshot(local,true,false,false,false,approvalKnown=true,maySetUp=true)
        val other=ArchiveDeviceSnapshot(remote,false,false,false,false,approvalKnown=true)
        val context=ArchiveEnrollmentSnapshot(8,3,local,listOf(first,other))
        val state=mutableStateOf(PeerChatUiState(demo=false,account=AccountUi("8","Test"),archiveEnrollment=context))
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){Column(Modifier.verticalScroll(rememberScrollState())){ArchiveDevicesSection(state.value,actions::add)}}}
        compose.onAllNodesWithText("Set up History Sync",useUnmergedTree=true).assertCountEquals(2)
        compose.onNodeWithText("Set up",substring=false).performScrollTo().performClick()
        compose.onNodeWithText("Confirm history authorization").assertExists()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Cancel").performClick()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Set up",substring=false).performScrollTo().performClick()
        compose.onNodeWithText("Confirm",substring=false).performClick()
        compose.runOnIdle{
            assertEquals(UiAction.ArchiveConsent(context,ArchiveDeviceAction.SetUp,local,true,true),actions.single())
            state.value=state.value.copy(archiveEnrollment=context.copy(devices=listOf(first.copy(maySetUp=false,approvedIssuer=true,authorized=true,grantedRead=true,grantedServe=true,mayAuthorize=true,localGrantPresent=true,localGrantRead=true,localGrantServe=true),other.copy(mayAuthorize=true))))
        }
        compose.onNodeWithText("Set up",substring=false).assertDoesNotExist()
        compose.onNodeWithText("Authorize",substring=false).performScrollTo().performClick()
        compose.onNodeWithText("Authorize device").performClick()
        compose.onNodeWithText("Confirm",substring=false).performClick()
        compose.runOnIdle{val command=actions.last() as UiAction.ArchiveConsent;assertEquals(remote,command.device);assertTrue(command.read&&command.serve);assertEquals(ArchiveDeviceAction.Authorize,command.action)}
    }
    @Test fun unverifiedAndDisabledDevicesNeverOfferFirstUseOrAuthorize(){
        val id="c".repeat(64)
        val state=PeerChatUiState(demo=false,account=AccountUi("8","Test"),archiveEnrollment=ArchiveEnrollmentSnapshot(8,3,id,listOf(ArchiveDeviceSnapshot(id,true,false,false,false,approvalKnown=true))))
        compose.setContent{PeerChatTheme(true){Column(Modifier.verticalScroll(rememberScrollState())){ArchiveDevicesSection(state){}}}}
        compose.onNodeWithText("Set up",substring=false).assertDoesNotExist()
        compose.onNodeWithText("Authorize",substring=false).assertDoesNotExist()
        compose.onNodeWithText("Approve from an enabled trusted history device.",substring=true).assertExists()
    }
    @Test fun committedBatchProgressUpdatesWithoutReopeningAndDoesNotCountScansAsReceipts(){
        val id="b".repeat(64)
        val context=ArchiveEnrollmentSnapshot(8,3,id,listOf(ArchiveDeviceSnapshot(id,true,true,true,false,authorized=true)))
        val state=mutableStateOf(PeerChatUiState(demo=false,account=AccountUi("8","Test"),archiveEnrollment=context,archiveProgress=ArchiveSyncProgress(state="Synchronizing history",running=true,source=id,processed=500,received=2)))
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){Column(Modifier.verticalScroll(rememberScrollState())){ArchiveDevicesSection(state.value,actions::add)}}}
        compose.onNodeWithText("2 new messages received this session",substring=true).assertExists()
        compose.onNodeWithText("Receive history").assertDoesNotExist()
        compose.onNodeWithText("Refresh history devices").assertDoesNotExist()
        compose.runOnIdle{state.value=state.value.copy(archiveProgress=state.value.archiveProgress.copy(received=10,conversations=2))}
        compose.onNodeWithText("10 new messages received this session · 2 conversations completed").assertExists()
        compose.runOnIdle{assertTrue(actions.isEmpty());state.value=state.value.copy(archiveProgress=state.value.archiveProgress.copy(state="History synchronized",running=false,complete=true,lastSuccessful=1791740000000))}
        compose.onNodeWithText("Phase: Complete",substring=true).assertExists()
        compose.onNodeWithText("Last successful sync: Not recorded").assertDoesNotExist()
    }
}
