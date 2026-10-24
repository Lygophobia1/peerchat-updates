package com.peerchat.android.ui

import com.peerchat.android.application.MessageSnapshot

/** View-only references, never storage. Whole inactive windows are evicted so a
 * retained window never acquires a paging gap. The visible window is protected:
 * until bidirectional UI paging exists, its scroll anchors must not be removed.
 * Limits apply again immediately when that window is left. */
internal class ConversationHistoryCache<K, V>(
    private val maxRows: Int,
    private val maxBytes: Long,
    private val bytes: (V) -> Long,
) {
    private data class Entry<V>(val rows: List<V>, val bytes: Long)
    private val entries = linkedMapOf<K, Entry<V>>()
    private var selected: K? = null
    var rowCount = 0; private set
    var retainedBytes = 0L; private set
    init { require(maxRows >= 0 && maxBytes >= 0) }
    operator fun get(key: K): List<V>? = entries[key]?.rows
    fun select(key: K?): Set<K> {
        selected = key
        if (key != null) entries.remove(key)?.let { entries[key] = it }
        return trim()
    }
    fun put(key: K, rows: List<V>): Set<K> {
        remove(key)
        val retained = rows.toList()
        val weight = retained.sumOf { bytes(it).coerceAtLeast(0) }
        entries[key] = Entry(retained, weight)
        rowCount += retained.size; retainedBytes += weight
        return trim()
    }
    private fun remove(key: K) {
        entries.remove(key)?.let { rowCount -= it.rows.size; retainedBytes -= it.bytes }
    }
    fun evict(key:K){remove(key);if(selected==key)selected=null}
    private fun trim(): Set<K> {
        val evicted = linkedSetOf<K>()
        while (rowCount > maxRows || retainedBytes > maxBytes) {
            val victim = entries.keys.firstOrNull { it != selected } ?: break
            remove(victim); evicted.add(victim)
        }
        return evicted
    }
    fun clear() { entries.clear(); selected = null; rowCount = 0; retainedBytes = 0 }
}

// Conservative retained payload estimate without allocating a UTF-8 copy. This
// is a cache budget, not a claim about exact JVM heap size/shared string storage.
internal fun messageHistoryBytes(row: MessageSnapshot): Long =
    256L + 2L * (row.id.length.toLong() + row.senderDevice.length + row.recipientDevice.length + row.text.length) +
        (row.deliveryTargets?.targets?.sumOf { 64L + 2L * it.deviceId.length } ?: 0L)
