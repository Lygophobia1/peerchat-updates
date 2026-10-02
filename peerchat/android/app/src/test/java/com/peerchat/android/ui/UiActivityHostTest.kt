package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import com.peerchat.android.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w360dp-h800dp-port")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class UiActivityHostTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()
    @Test fun activityRecreationRetainsUiNavigationAndDraft() {
        compose.onNodeWithTag("explore").performClick()
        compose.onNodeWithText("Sammy").performClick()
        compose.onNodeWithTag("composer").performTextInput("Draft survives rotation 👋\nSecond line")
        compose.activityRule.scenario.recreate()
        compose.onNodeWithTag("composer").assertTextContains("Draft survives rotation", substring = true)
        compose.onNodeWithContentDescription("Conversation details").assertExists()
    }
    @Test fun passwordDoesNotSurviveActivityRecreation() {
        compose.onNodeWithText("Sign in").performClick()
        compose.onNodeWithTag("password").performTextInput("test-only-secret")
        compose.activityRule.scenario.recreate()
        compose.onNodeWithTag("password").assertTextEquals("Password", "")
        compose.onNodeWithTag("sign-in-submit").performClick()
        compose.onNodeWithTag("login-error").assertTextContains("Enter your username", substring = true)
    }
}
