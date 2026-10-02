package com.peerchat.android.platform

import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteCursor
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteProgram
import java.io.File
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

// Platform execution/crypto only. Native Storage retains ALL schema, record,
// history/outbox, ordering, dedupe and transaction policy.
class AndroidStoragePlatform(context: Context, private val profile: String) : AutoCloseable {
    private val state = ProfileSecurity(context.applicationContext, profile)
    private val directory = File(context.noBackupFilesDir, "profiles/" + state.id.joinToString("") { "%02x".format(it) })
    private val file = File(directory, "peerchat.sqlite")
    private val identity: AndroidIdentityProvider
    private val records: AndroidRecordProtector
    private val database: SQLiteDatabase
    private var nextStatement = 1L
    private val statements = mutableMapOf<Long, Statement>()
    private var closed = false
    init {
        // A persisted database must not cause a replacement identity to be
        // generated when both its key and public metadata have disappeared.
        if (file.exists() && (state.read("identity.public") == null || !state.store().containsAlias(state.identityAlias)))
            throw ProviderFailure("IDENTITY_RECOVERY_REQUIRED")
        identity = AndroidIdentityProvider(context, profile)
        records = AndroidRecordProtector(context, profile)
        if (!directory.isDirectory && !directory.mkdirs()) throw ProviderFailure("STORAGE_OPEN_FAILED")
        database = SQLiteDatabase.openDatabase(file.path, null, SQLiteDatabase.CREATE_IF_NECESSARY)
    }
    private class Statement(val sql: String) {
        val bindings = sortedMapOf<Int, Any>()
        var started = false
        var cursor: Cursor? = null
    }
    private fun text(bytes: ByteArray): String = Charsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT)
        .decode(ByteBuffer.wrap(bytes)).toString()
    private fun checkOpen() { if (closed) throw ProviderFailure("STORAGE_CLOSED") }
    private fun statement(id: Long): Statement { checkOpen(); return statements[id] ?: throw ProviderFailure("INVALID_STATEMENT") }
    fun scope(): ByteArray = profile.toByteArray(Charsets.UTF_8)
    fun publicKey(): ByteArray = identity.publicRepresentation()
    fun sign(bytes: ByteArray): ByteArray = identity.sign(bytes)
    fun createPeerTls(incoming: Boolean, expectedPeer: ByteArray): com.peerchat.android.platform.tls.PeerTls {
        checkOpen()
        invalid(expectedPeer.size == 64 && expectedPeer.all { it in 48..57 || it in 97..102 })
        return com.peerchat.android.platform.tls.PeerTls(
            com.peerchat.android.platform.tls.AndroidTlsIdentity(identity), incoming,
            expectedPeer.toString(Charsets.US_ASCII))
    }
    fun protect(bytes: ByteArray, context: ByteArray): ByteArray = records.protect(bytes, context)
    fun unprotect(bytes: ByteArray, context: ByteArray): ByteArray = records.unprotect(bytes, context)
    fun execute(bytes: ByteArray) {
        checkOpen(); invalid(bytes.size <= 32768)
        val sql = text(bytes)
        // Native execute uses fixed schema/PRAGMA/transaction statements only;
        // all values use bound statements. Reject quoted literals/comments here
        // rather than ambiguously splitting arbitrary SQL at semicolons.
        invalid(!sql.contains('\u0000') && !sql.contains('"') && !sql.contains('\'') && !sql.contains("--") && !sql.contains("/*"))
        sql.split(';').map { it.trim() }.filter { it.isNotEmpty() }.forEach { database.execSQL(it) }
    }
    fun prepare(bytes: ByteArray): Long {
        checkOpen(); invalid(bytes.size in 1..32768 && statements.size < 128)
        val sql = text(bytes); invalid(!sql.contains('\u0000') && !sql.contains(';'))
        if (nextStatement == Long.MAX_VALUE) throw ProviderFailure("STATEMENT_LIMIT")
        val id = nextStatement++; statements[id] = Statement(sql); return id
    }
    fun bindBytes(id: Long, index: Int, bytes: ByteArray, blob: Boolean) {
        val s = statement(id); invalid(!s.started && index in 1..128 && bytes.size <= 1048576)
        s.bindings[index] = if (blob) bytes.copyOf() else text(bytes)
    }
    fun bindNumber(id: Long, index: Int, number: Long) {
        val s = statement(id); invalid(!s.started && index in 1..128); s.bindings[index] = number
    }
    private fun bind(s: Statement, program: SQLiteProgram) {
        s.bindings.forEach { (index, value) -> when(value) {
            is String -> program.bindString(index, value)
            is ByteArray -> program.bindBlob(index, value)
            is Long -> program.bindLong(index, value)
            else -> throw ProviderFailure("INVALID_BINDING")
        } }
    }
    fun row(id: Long): Boolean {
        val s = statement(id)
        if (!s.started) {
            s.started = true
            if (s.sql.trimStart().startsWith("SELECT", ignoreCase = true)) {
                s.cursor = database.rawQueryWithFactory({ _, driver, editTable, query ->
                    bind(s, query); SQLiteCursor(driver, editTable, query)
                }, s.sql, emptyArray(), "")
            } else database.compileStatement(s.sql).use { bind(s, it); it.execute() }
        }
        return s.cursor?.moveToNext() ?: false
    }
    private fun cursor(id: Long, index: Int): Cursor {
        val cursor = statement(id).cursor ?: throw ProviderFailure("NO_RESULT_ROW")
        invalid(!cursor.isBeforeFirst && !cursor.isAfterLast && index in 0 until cursor.columnCount && !cursor.isNull(index)); return cursor
    }
    fun bytes(id: Long, index: Int): ByteArray = cursor(id, index).getBlob(index).also { invalid(it.size in 1..1048576) }
    fun text(id: Long, index: Int): ByteArray = cursor(id, index).getString(index).toByteArray(Charsets.UTF_8)
    fun number(id: Long, index: Int): Long = cursor(id, index).getLong(index)
    fun finish(id: Long) { statements.remove(id)?.cursor?.close() }
    override fun close() {
        if (!closed) { statements.values.forEach { it.cursor?.close() }; statements.clear(); database.close(); closed = true }
    }
}
