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
class MailboxReenrollmentUiTest {
    @get:Rule val compose=createComposeRule()
    private val value=mailboxReenrollment(NativeRecordWriter(3).also{w->
        w.text("PeerChat/MailboxReenrollment/v1");w.number(4);w.number(3);w.number(19)
        w.text("a".repeat(64));w.text("b".repeat(32));w.number(2);w.text("server.example:7443")
        w.text("c".repeat(64));w.text("d".repeat(32));w.blob(byteArrayOf(1))
    }.bytes())
    @Test fun explicitConsentCarriesExactDisplayedScope(){
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,mailboxReenrollment=value),actions::add)}}
        compose.onNodeWithText("Original account 19 → current account 4").assertExists()
        compose.onNodeWithText("Re-enroll mailbox").assertIsDisplayed().performClick()
        compose.onNodeWithText("Keep existing binding").assertIsDisplayed().performClick()
        compose.runOnIdle{assertEquals(listOf(UiAction.ReenrollMailbox(value,true),UiAction.ReenrollMailbox(value,false)),actions)}
    }
    @Test fun busyConsentCannotSubmitTwice(){
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,mailboxReenrollment=value,mailboxRecoveryBusy=true),{})}}
        compose.onNodeWithText("Re-enroll mailbox").assertIsNotEnabled()
        compose.onNodeWithText("Keep existing binding").assertIsNotEnabled()
    }
    @Test fun deviceVerificationHasPriorityOverMailboxConsent(){
        val device=DeviceVerification(4,3,7,5,"Sammy","d".repeat(64),listOf("e".repeat(64)),"proof")
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,deviceVerification=device,mailboxReenrollment=value),{})}}
        compose.onNodeWithText("Verify and Accept").assertExists()
        compose.onNodeWithText("Re-enroll mailbox").assertDoesNotExist()
    }
    @Test fun serverTrustHasPriorityOverMailboxConsent(){
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,serverTrust=ServerTrustUi(ServerUi("server.example"),"f".repeat(64)),mailboxReenrollment=value),{})}}
        compose.onNodeWithText("Trust Server").assertExists()
        compose.onNodeWithText("Re-enroll mailbox").assertDoesNotExist()
    }
    @Test fun errorWithoutOfferDoesNotClaimSuccess(){
        compose.setContent{PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,mailboxRecoveryError="Current enrollment unavailable"),{})}}
        compose.onNodeWithText("Offline mailbox needs attention").assertExists()
        compose.onNodeWithText("Current enrollment unavailable").assertExists()
        compose.onNodeWithText("Re-enroll mailbox").assertDoesNotExist()
    }
    @Test fun unauthenticatedProjectionRemovesStaleOffer(){
        val previous=PeerChatUiState(mailboxReenrollment=value)
        assertNull(projectMessaging(MessagingSnapshot(mailboxReenrollment=value),previous).mailboxReenrollment)
    }
}
