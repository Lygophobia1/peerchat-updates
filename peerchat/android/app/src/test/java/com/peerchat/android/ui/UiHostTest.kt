package com.peerchat.android.ui

import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w360dp-h800dp-port")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class UiHostTest : UiFlowChecks()
