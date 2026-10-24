package com.peerchat.android.platform

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

@org.junit.runner.RunWith(org.robolectric.RobolectricTestRunner::class)
@org.robolectric.annotation.Config(sdk=[34])
class CacheAppendBatchTest {
    private fun chunk(offset:Long,lease:String="a",operation:String="cache_dm_append")=operation to JSONObject().put("lease",lease).put("offset",offset).put("data","AQID")
    @Test fun negotiatedContiguousWindowOnly(){
        val requests=(0 until 8).map{chunk(it*3L)}
        assertEquals(8,cacheAppendBatchCount(requests,8))
        assertEquals(0,cacheAppendBatchCount(requests,0))
        assertEquals(0,cacheAppendBatchCount(requests,4))
        assertEquals(0,cacheAppendBatchCount(requests+chunk(24),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0)),8))
        assertFalse(requests.first().second.has("batchCount"))
    }
    @Test fun rejectsMixedLeaseOperationAndOffsets(){
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0),chunk(3,"b")),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0),chunk(3,operation="cache_append")),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0),chunk(2)),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(-3),chunk(0)),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(Long.MAX_VALUE-1),chunk(0)),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0).also{it.second.put("batchCount",2)},chunk(3)),8))
    }
    @Test fun preservesLargeOffsetsAndBoundsPayload(){
        val base=5L*1024*1024*1024
        assertEquals(2,cacheAppendBatchCount(listOf(chunk(base),chunk(base+3)),8))
        val full=chunk(0).also{it.second.put("data","A".repeat(87382))}
        assertEquals(2,cacheAppendBatchCount(listOf(full,chunk(65536)),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0).also{it.second.put("data","A")},chunk(1)),8))
        assertEquals(0,cacheAppendBatchCount(listOf(chunk(0).also{it.second.put("data","A".repeat(87384))},chunk(65538)),8))
    }
}
