package com.peerchat.android.platform

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class CacheAppendFieldsTest {
    @Test fun fullChunkAndLargeOffsetSurviveWithoutMutatingBorrowedCorrelation() {
        val chunk=java.util.Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(65536){it.toByte()})
        val original=JSONObject().put("requestId","native-id").put("lease","lease-id")
            .put("offset",5L*1024*1024*1024).put("data",chunk)
        val copy=copyCacheAppendFields(original).put("requestId","rpc-id").put("sessionToken","test-session")
        val wire=JSONObject(copy.toString())
        assertEquals(chunk,wire.getString("data"))
        assertEquals(5L*1024*1024*1024,wire.getLong("offset"))
        assertEquals("rpc-id",wire.getString("requestId"))
        assertEquals("native-id",original.getString("requestId"))
        assertFalse(original.has("sessionToken"))
    }
    @Test(expected=IllegalArgumentException::class) fun mutableNestedFieldsCannotBeAliased() {
        copyCacheAppendFields(JSONObject().put("data",JSONObject()))
    }
}
