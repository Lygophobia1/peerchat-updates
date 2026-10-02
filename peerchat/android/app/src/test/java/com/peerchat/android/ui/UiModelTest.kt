package com.peerchat.android.ui

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.*
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class UiModelTest {
    @Test fun honestLoginLoadingValidationFailureAndNoCredentialRetention() = runTest {
        Dispatchers.setMain(StandardTestDispatcher(testScheduler))
        try {
            val model = PeerChatUiModel()
            model.dispatch(UiAction.Navigate(Route.Login))
            model.login("", "")
            assertFalse(model.state.value.loginBusy)
            assertNotNull(model.state.value.loginError)
            model.login("demo", "test-only")
            assertTrue(model.state.value.loginBusy)
            advanceUntilIdle()
            assertFalse(model.state.value.loginBusy)
            assertEquals(Route.Login, model.state.value.route)
            assertNull(model.state.value.account)
            assertTrue(model.state.value.loginError!!.contains("not connected"))
            assertFalse(model.state.value.toString().contains("test-only"))
        } finally { Dispatchers.resetMain() }
    }
    @Test fun backendExceptionIsBoundedAndCannotLeakItsMessage() = runTest {
        Dispatchers.setMain(StandardTestDispatcher(testScheduler))
        try {
            val model = PeerChatUiModel(accounts = object : AccountAccess {
                override suspend fun signIn(username: String, password: String, server: ServerUi): LoginResult {
                    throw IllegalStateException("do-not-expose-credentials")
                }
            })
            model.login("demo", "test-only")
            advanceUntilIdle()
            assertFalse(model.state.value.loginBusy)
            assertFalse(model.state.value.loginError!!.contains("do-not-expose"))
            assertNull(model.state.value.account)
        } finally { Dispatchers.resetMain() }
    }
    @Test fun olderHistoryLoadingCannotAppendToAnotherConversation() = runTest {
        Dispatchers.setMain(StandardTestDispatcher(testScheduler))
        try {
            val model = PeerChatUiModel()
            model.dispatch(UiAction.Select("demo-dm-sammy"))
            model.dispatch(UiAction.BeginOlder)
            assertTrue(model.state.value.olderLoading)
            model.dispatch(UiAction.Select("demo-dm-alex"))
            advanceUntilIdle()
            assertTrue(model.state.value.selected!!.hasOlder)
            assertFalse(model.state.value.olderLoading)
            model.dispatch(UiAction.BeginOlder)
            advanceUntilIdle()
            assertFalse(model.state.value.selected!!.hasOlder)
        } finally { Dispatchers.resetMain() }
    }
}
