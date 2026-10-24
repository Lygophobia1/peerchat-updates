package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class TransferWorkLifetimeTest {
    @Test fun foregroundHandoffPreservesBorrowedResponseButRevocationDoesNot() {
        val lifetime=TransferWorkLifetime()
        lifetime.update(true)
        val borrowed=lifetime.revision
        lifetime.update(true) // transfer service joins UI
        lifetime.update(true) // UI leaves; service still admits transfers
        assertEquals(borrowed,lifetime.revision)
        lifetime.update(false) // no transfer owner, logout, or policy revocation
        lifetime.update(true)
        assertNotEquals(borrowed,lifetime.revision)
    }
}
