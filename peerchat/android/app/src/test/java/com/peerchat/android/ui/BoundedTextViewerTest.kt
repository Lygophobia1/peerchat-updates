package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class BoundedTextViewerTest {
    @get:Rule val compose=createComposeRule()
    @Test fun unicodeChunksKeepExactContentAndBoundLayout(){
        val text="a".repeat(2047)+"😀日本語"+"b".repeat(9000)
        val rows=textViewRows(text)
        assertEquals(text,rows.joinToString(""))
        assertTrue(rows.all{it.length<=2048&&it.toByteArray().toString(Charsets.UTF_8)==it})
        assertFalse(codeLike("A long ordinary paragraph.\nAnother prose line."))
        assertTrue(codeLike("import java.io.File;\npublic class Test {}"))
        assertEquals(listOf("one\r","","日本語",""),textViewRows("one\r\n\n日本語\n"))
    }
    @Test fun fullTextIsScrollableExpandableAndReadOnly(){
        val text=(1..500).joinToString("\n"){"Line $it · 日本語 😀"}
        compose.setContent{PeerChatTheme(true){BoundedTextViewer(text,false,"Long message")}}
        compose.onNodeWithText("Line 1 · 日本語 😀").assertExists()
        compose.onNodeWithTag("text-scroll").performScrollToIndex(499)
        compose.onNodeWithText("Line 500 · 日本語 😀").assertExists()
        compose.onNodeWithText("Expand").performClick()
        compose.onNodeWithText("Show less").assertExists()
        compose.onNodeWithText("Open full").performClick()
        compose.onNodeWithText("Long message").assertExists()
        compose.onNodeWithText("Close").performClick()
        compose.onNodeWithText("Copy").assertExists()
    }
    @Test fun typedTextUsesLongMessageCardInsteadOfFilename(){
        val row=com.peerchat.android.application.AttachmentSnapshot("c".repeat(32),"b".repeat(64),"message.peerchat-text",70000,4,true,70000,1,"","")
        compose.setContent{PeerChatTheme(true){AttachmentCard(row,{},"Actual prose\nMultiline 日本語")}}
        compose.onNodeWithText("Long message").assertExists()
        compose.onNodeWithText("message.peerchat-text").assertDoesNotExist()
        compose.onNodeWithText("Actual prose").assertExists()
        compose.onNodeWithText("Multiline 日本語").assertExists()
    }
}
