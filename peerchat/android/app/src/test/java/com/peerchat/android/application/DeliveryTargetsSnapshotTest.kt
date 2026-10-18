package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class DeliveryTargetsSnapshotTest {
    private val id="c".repeat(32)
    private val secondId="d".repeat(32)
    private val local="a".repeat(64)
    private val remote="b".repeat(64)
    private fun history(extension:(NativeRecordWriter.()->Unit)?=null):ByteArray=NativeRecordWriter(2).apply {
        number(2)
        listOf(id,secondId).forEachIndexed{index,messageId->
            text(messageId);text(local);text(remote);number(100+index.toLong());number(5+index.toLong())
            number(7+index.toLong());number(2);text("message $index")
        }
        if(extension!=null){text("PeerChat/DeliveryTargetsSnapshot/v1");extension()}
    }.bytes()
    private fun NativeRecordWriter.manifest(messageId:String=id,unresolved:Long=0,targets:List<Pair<String,Long>> = listOf(remote to 0L)) {
        text(messageId);number(unresolved);number(targets.size.toLong())
        targets.forEach{(device,status)->text(device);number(status)}
    }
    @Test fun legacyHistoryHasNoInventedTargetsAndPreservesRows() {
        val rows=messageSnapshots(history())
        assertEquals(listOf(id,secondId),rows.map{it.id})
        assertTrue(rows.all{it.deliveryTargets==null})
        assertEquals(listOf(7L,8L),rows.map{it.sequence})
        assertEquals(listOf("message 0","message 1"),rows.map{it.text})
    }
    @Test fun partialAndFullAcknowledgmentsRemainSeparateFromAggregateDelivery() {
        val targets=listOf(remote to 1L,"e".repeat(64) to 0L,"f".repeat(64) to 2L)
        val rows=messageSnapshots(history{number(1);manifest(targets=targets)})
        assertEquals(targets.map{it.first},rows.first().deliveryTargets!!.targets.map{it.deviceId})
        assertEquals(listOf(DeliveryTargetStatus.Acknowledged,DeliveryTargetStatus.Pending,DeliveryTargetStatus.Unavailable),
            rows.first().deliveryTargets!!.targets.map{it.status})
        assertEquals(2L,rows.first().delivery)
        assertNull(rows.last().deliveryTargets)
        val full=messageSnapshots(history{number(1);manifest(targets=targets.map{it.first to 1L})}).first()
        assertTrue(full.deliveryTargets!!.targets.all{it.status==DeliveryTargetStatus.Acknowledged})
    }
    @Test fun unresolvedSelectionIsExplicitAndEmptyProjectionDiffersFromLegacy() {
        val rows=messageSnapshots(history{number(2);manifest(unresolved=1,targets=emptyList());manifest(secondId,targets=emptyList())})
        assertTrue(rows.first().deliveryTargets!!.selectionUnresolved)
        assertTrue(rows.first().deliveryTargets!!.targets.isEmpty())
        assertFalse(rows.last().deliveryTargets!!.selectionUnresolved)
        assertTrue(messageSnapshots(history{number(0)}).all{it.deliveryTargets==null})
    }
    @Test fun rejectsInvalidOrAmbiguousManifestWithoutPartialProjection() {
        val malformed=listOf<NativeRecordWriter.()->Unit>(
            {number(3)},
            {number(1);manifest("0".repeat(32))},
            {number(2);manifest();manifest()},
            {number(1);manifest(unresolved=2)},
            {number(1);manifest(targets=listOf(remote to 3L))},
            {number(1);manifest(targets=listOf("short" to 0L))},
            {number(1);manifest(targets=listOf("B".repeat(64) to 0L))},
            {number(1);manifest(targets=listOf(remote to 0L,remote to 1L))},
            {number(1);text(id);number(0);number(33)},
            {number(1);text(id);number(0);number(1)},
            {number(0);number(0)}
        )
        malformed.forEach{extension->assertThrows(IllegalArgumentException::class.java){messageSnapshots(history(extension))}}
        val truncated=history{number(1);manifest()}.dropLast(1).toByteArray()
        assertThrows(IllegalArgumentException::class.java){messageSnapshots(truncated)}
    }
}
