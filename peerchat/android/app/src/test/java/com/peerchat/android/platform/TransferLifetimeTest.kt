package com.peerchat.android.platform

import com.peerchat.android.application.AttachmentSnapshot
import org.junit.Assert.*
import org.junit.Test

class TransferLifetimeTest {
    private val id="a".repeat(32)
    private fun row(state:Int)=AttachmentSnapshot(id,"b".repeat(64),"test",100,state,false,50,0,"","")
    @Test fun explicitAdmissionBoundedCoalescing(){
        val life=TransferLifetime(100)
        assertFalse(life.add("bad"));assertTrue(life.add(id));assertTrue(life.add(id));assertEquals(1,life.ids.size)
        assertFalse(life.finished(listOf(row(3)),1000))
        assertFalse(life.finished(emptyList(),1000))
        assertTrue(life.finished(listOf(row(4)),1000))
        for(state in listOf(5,6,8))assertTrue(life.finished(listOf(row(state)),1000))
        assertTrue(life.finished(listOf(row(3)),100+TransferLifetime.BUDGET_MS))
        assertTrue(life.finished(emptyList(),10101))
    }
    @Test fun simultaneousTransfersAndReconnect(){
        val life=TransferLifetime(0);life.add(id);life.add("c".repeat(32))
        assertFalse(life.finished(listOf(row(4),row(7).copy(id="c".repeat(32))),20000))
        assertTrue(life.finished(listOf(row(4),row(5).copy(id="c".repeat(32))),20000))
        assertEquals(2,life.ids.size)
    }
}
