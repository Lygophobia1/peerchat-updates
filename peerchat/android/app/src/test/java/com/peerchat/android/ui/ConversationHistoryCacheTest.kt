package com.peerchat.android.ui

import com.peerchat.android.application.MessageSnapshot
import org.junit.Assert.*
import org.junit.Test

class ConversationHistoryCacheTest {
    @Test fun countAndBytesShareOneBudgetAndProjectionDoesNotTouchLru() {
        val cache = ConversationHistoryCache<String, String>(3, 12) { it.length.toLong() }
        cache.put("a", listOf("aaaa")); cache.put("b", listOf("bbbb"))
        cache.select("a"); cache.select(null)
        assertEquals(listOf("bbbb"), cache["b"])
        assertEquals(setOf("b"), cache.put("c", listOf("ccccc")))
        assertNotNull(cache["a"]); assertNull(cache["b"])
        assertEquals(setOf("a"), cache.put("d", listOf("x", "x")))
        assertEquals(3, cache.rowCount); assertEquals(7L, cache.retainedBytes)
    }
    @Test fun visibleWindowAndScrollRowsSurviveUntilDeselectThenStorageCanReload() {
        val cache = ConversationHistoryCache<String, String>(2, 8) { it.length.toLong() }
        cache.select("a"); cache.put("a", listOf("old", "anchor", "new"))
        assertEquals(setOf("b"), cache.put("b", listOf("b")))
        assertEquals(listOf("old", "anchor", "new"), cache["a"])
        assertEquals(setOf("a"), cache.select("b"))
        assertEquals(0, cache.rowCount); assertEquals(0L, cache.retainedBytes)
        cache.select("a"); cache.put("a", listOf("reload"))
        assertEquals(listOf("reload"), cache["a"])
    }
    @Test fun hugeTextUsesByteBudgetAndAccountClearDropsAllReferences() {
        val row = MessageSnapshot("id", "sender", "recipient", 1, 1, 1, 0, "x".repeat(512 * 1024))
        assertTrue(messageHistoryBytes(row) > 1024 * 1024)
        val cache = ConversationHistoryCache<Long, MessageSnapshot>(512, 1024 * 1024, ::messageHistoryBytes)
        assertEquals(setOf(8L), cache.put(8L, listOf(row)))
        cache.select(8L); cache.put(8L, listOf(row)); assertNotNull(cache[8L])
        cache.clear(); assertNull(cache[8L]); assertEquals(0, cache.rowCount); assertEquals(0L, cache.retainedBytes)
        assertEquals(setOf(8L), cache.put(8L, listOf(row)))
    }
}
