package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test

class TransferPreparationLifetimeTest {
    private val token="a".repeat(32)
    private val id="b".repeat(32)
    @Test fun preparationStaysActiveAndHandsOffWithoutExtendingBudget() {
        val life=TransferLifetime(0)
        assertTrue(life.addPreparation(token))
        assertFalse(life.finished(emptyList(),60_000))
        assertTrue(life.finishPreparation(token,id,60_000))
        assertEquals(setOf(id),life.ids)
        assertTrue(life.preparingIds.isEmpty())
        assertFalse(life.finished(emptyList(),60_001))
        assertTrue(life.finished(emptyList(),70_001))
        assertTrue(life.finished(emptyList(),TransferLifetime.BUDGET_MS))
    }
    @Test fun failedOrCancelledPreparationLeavesNoPhantomTransfer() {
        val life=TransferLifetime(0);life.addPreparation(token)
        assertTrue(life.finishPreparation(token,null,500))
        assertTrue(life.ids.isEmpty());assertTrue(life.finished(emptyList(),500))
        assertFalse(life.finishPreparation(token,id,600))
        assertTrue(life.ids.isEmpty())
    }
    @Test fun preparationCannotExtendFiniteLifetimeOrOverflowAdmission() {
        val life=TransferLifetime(0)
        repeat(64){assertTrue(life.addPreparation(it.toString(16).padStart(32,'0')))}
        assertFalse(life.addPreparation(token))
        assertTrue(life.finished(emptyList(),TransferLifetime.BUDGET_MS))
    }
    @Test fun cancelHandlesAreBoundedRemovedAndInvokedOutsideRegistryLock() {
        val registry=TransferPreparations(1);var cancelled=0
        assertTrue(registry.add(token){cancelled++;assertTrue(registry.add(id){})})
        assertFalse(registry.add(id){})
        registry.cancel(listOf(token));assertEquals(1,cancelled)
        registry.cancel(listOf(token));assertEquals(1,cancelled)
        assertTrue(registry.remove(id));assertFalse(registry.remove(id))
        assertFalse(registry.add("bad"){})
    }
    @Test fun deletedBubbleDoesNotFinishActiveTransferLifetime() {
        val life=TransferLifetime(0);life.add(id)
        assertFalse(life.finished(emptyList(),60_000,setOf(id)))
        assertTrue(life.finished(emptyList(),60_001,emptySet()))
        assertTrue(life.finished(emptyList(),TransferLifetime.BUDGET_MS,setOf(id)))
    }
}
