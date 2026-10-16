package com.peerchat.android.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.core.app.ActivityOptionsCompat
import androidx.test.core.app.ApplicationProvider
import java.io.ByteArrayOutputStream
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w360dp-h800dp-port")
class DiagnosticsExportUiTest {
    @get:Rule val compose=createComposeRule()
    @Test fun largeReportSaveUsesFrozenSnapshotAndRemainsIndependentOfClipboard(){
        val context=ApplicationProvider.getApplicationContext<Context>()
        var requestCode=0;var picker:Intent?=null
        val registry=object:ActivityResultRegistry(){
            override fun <I,O> onLaunch(code:Int,contract:ActivityResultContract<I,O>,input:I,options:ActivityOptionsCompat?){
                requestCode=code;picker=contract.createIntent(context,input)
            }
        }
        val owner=object:ActivityResultRegistryOwner{override val activityResultRegistry=registry}
        val complete="stage=message_delivery message_id=${"1".repeat(32)}\n".repeat(20_000)+"original_snapshot_last_record 日本語 👋"
        assertTrue(complete.length>1_048_576)
        var state by mutableStateOf(PeerChatUiState(demo=false,diagnostics=complete))
        compose.setContent{CompositionLocalProvider(LocalActivityResultRegistryOwner provides owner){PeerChatTheme(true){DiagnosticsScreen(state,{})}}}
        compose.onNodeWithTag("diagnostics-copy").assertIsNotEnabled()
        compose.onNodeWithTag("diagnostics-export").assertIsDisplayed().assertIsEnabled().performClick()
        compose.runOnIdle{assertEquals(Intent.ACTION_CREATE_DOCUMENT,picker!!.action);assertEquals("text/plain",picker!!.type);state=state.copy(diagnostics="later snapshot")}
        val target=Uri.parse("content://system.documents/diagnostics/fixture.txt")
        val output=ByteArrayOutputStream()
        org.robolectric.Shadows.shadowOf(context.contentResolver).registerOutputStream(target,output)
        compose.runOnIdle{registry.dispatchResult(requestCode,Activity.RESULT_OK,Intent().setData(target))}
        compose.waitUntil(10_000){compose.onAllNodesWithText("Complete diagnostic snapshot saved.").fetchSemanticsNodes().isNotEmpty()}
        val saved=output.toString("UTF-8")
        assertTrue(saved.endsWith(complete+"\n"));assertFalse(saved.contains("later snapshot"))
        compose.onNodeWithTag("diagnostics-export").assertIsEnabled()
    }
}
