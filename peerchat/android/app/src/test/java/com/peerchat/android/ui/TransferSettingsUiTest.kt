package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.TransferSettingsSnapshot
import org.junit.*
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w360dp-h800dp-port")
class TransferSettingsUiTest {
    @get:Rule val compose=createComposeRule()
    @Test fun existingCustomRatesArePreservedUntilExplicitSave(){
        val saved=TransferSettingsSnapshot(32768,4,1_234_567,3_456_789)
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(false){TransferSettingsScreen(PeerChatUiState(transferSettings=saved),actions::add)}}
        compose.onNodeWithTag("upload-custom").performScrollTo().assertTextContains("1.234567")
        compose.onNodeWithTag("upload-Unlimited").performScrollTo().performClick()
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithTag("save-transfer-settings").performScrollTo().performClick()
        compose.runOnIdle{assertEquals(listOf(UiAction.SaveTransferSettings(saved.copy(uploadBitsPerSecond=0))),actions)}
    }
    @Test fun decimalMbpsConversionIsExactAndRejectsInvalidLimits(){
        assertEquals("1.234567",transferMbps(1_234_567))
        assertEquals(1_234_567L,transferBits("Custom","1.234567"))
        assertEquals(1L,transferBits("Custom","0.000001"))
        assertEquals(10_000_000L,transferBits("10 Mbps",""))
        assertEquals(0L,transferBits("Unlimited",""))
        assertNull(transferBits("Custom","-1"));assertNull(transferBits("Custom","0"))
        assertNull(transferBits("Custom","0.0000001"));assertNull(transferBits("Custom","10001"))
    }
}
