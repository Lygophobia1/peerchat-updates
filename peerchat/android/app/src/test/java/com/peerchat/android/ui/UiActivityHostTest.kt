package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import android.content.Intent
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.rules.ActivityScenarioRule
import org.junit.rules.RuleChain
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
    private val activity = ActivityScenarioRule<MainActivity>(Intent(ApplicationProvider.getApplicationContext(),MainActivity::class.java).putExtra("peerchat_ui_demo",true))
    val compose = createEmptyComposeRule()
    @get:Rule val rules:RuleChain=RuleChain.outerRule(compose).around(activity)
    @Test fun activityRecreationRetainsUiNavigationAndDraft() {
        compose.onNodeWithTag("explore").performClick()
        compose.onNodeWithText("Sammy").performClick()
        compose.onNodeWithTag("composer").performTextInput("Draft survives rotation 👋\nSecond line")
        activity.scenario.recreate()
        compose.onNodeWithTag("composer").assertTextContains("Draft survives rotation", substring = true)
        compose.onNodeWithContentDescription("Conversation details").assertExists()
    }
    @Test fun passwordDoesNotSurviveActivityRecreation() {
        compose.onNodeWithText("Sign in").performClick()
        compose.onNodeWithTag("password").performTextInput("test-only-secret")
        activity.scenario.recreate()
        compose.onNodeWithTag("password").assertTextEquals("Password", "")
        compose.onNodeWithTag("sign-in-submit").performClick()
        compose.onNodeWithTag("login-error").assertTextContains("Enter your username", substring = true)
    }
}
