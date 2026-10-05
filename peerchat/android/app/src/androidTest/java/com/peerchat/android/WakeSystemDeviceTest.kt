package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.platform.WakeSystemDiagnostics
import org.junit.Test
import org.junit.runner.RunWith

/** Read-only probe. Does not authenticate, schedule/force a job, or modify data. */
@RunWith(AndroidJUnit4::class)
class WakeSystemDeviceTest {
    @Test fun observeThermalAndPendingWakeReasons() {
        WakeSystemDiagnostics.record(InstrumentationRegistry.getInstrumentation().targetContext,"wake_system_probe")
    }
}
