package com.peerchat.android.platform

import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteCursor
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteProgram
import java.io.File
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

/** Fixed native schema statements only, never SQL containing user values.
 * Current native triggers contain one body statement followed by END. Preserve
 * that internal semicolon; reject unfinished/unsupported bodies before executing.
 */
internal fun nativeSqlStatements(sql:String):List<String> {
    require(sql.length<=32768&&!sql.contains('\u0000')&&!sql.contains('"')&&!sql.contains('\'')&&!sql.contains("--")&&!sql.contains("/*"))
    val result=mutableListOf<String>()
    var trigger:String?=null
    for(part in sql.split(';').map{it.trim()}.filter{it.isNotEmpty()}) {
        val pending=trigger
        if(pending!=null){
            require(part.equals("END",ignoreCase=true)){"Unsupported native trigger body"}
            result.add("$pending; END;");trigger=null
        }else if(part.startsWith("CREATE TRIGGER ",ignoreCase=true)){
            require(Regex("\\bBEGIN\\b",RegexOption.IGNORE_CASE).containsMatchIn(part)){"Invalid native trigger"}
            trigger=part
        }else result.add(part)
    }
    require(trigger==null){"Incomplete native trigger"}
    return result
}

// Platform execution/crypto only. Native Storage retains ALL schema, record,
// history/outbox, ordering, dedupe and transaction policy.
class AndroidStoragePlatform(context: Context, private val profile: String, private val registryOnly: Boolean = false,private val registryDiagnostic:((String)->Unit)?=null) : AutoCloseable {
    private val applicationContext = context.applicationContext
    private val state = ProfileSecurity(context.applicationContext, profile)
    private val directory = File(context.noBackupFilesDir, "profiles/" + state.id.joinToString("") { "%02x".format(it) })
    private val file = File(directory, if (registryOnly) "device-key-registry.sqlite" else "peerchat.sqlite")
    private val recipient by lazy { AndroidRecipientKeyProvider(applicationContext, profile) }
    private val recipientEvidence=RecipientDiagnostic()
    val recipientDiagnostic:String get()=recipientEvidence.value
    fun beginMailboxRegistration(){recipientEvidence.begin()}
    private fun <T> recipientOperation(stage:String,operation:()->T):T {
        recipientEvidence.record("stage=$stage")
        try{return operation().also{recipientEvidence.record("stage=$stage result=ready")}}
        catch(failure:Exception){
            recipientEvidence.record("stage=$stage reason="+recipientFailureCode(failure))
            throw failure
        }
    }
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
    fun attachmentDirectory(): ByteArray {
        checkOpen()
        val cache = File(directory, "attachments")
        if (!cache.isDirectory && !cache.mkdir()) throw ProviderFailure("ATTACHMENT_CACHE_OPEN_FAILED")
        return cache.absolutePath.toByteArray(Charsets.UTF_8)
    }
    fun publicKey(): ByteArray = identity.publicRepresentation()
    fun sign(bytes: ByteArray): ByteArray = identity.sign(bytes)
    // Separate DB file avoids self-contention while a KeyManager lease spans
    // decryption and durable message acceptance on the normal profile database.
    fun keyRegistryProvider(): Any = recipientOperation("key_registry_provider") {
        checkOpen(); invalid(!registryOnly)
        AndroidStoragePlatform(applicationContext, profile, true){recipientEvidence.record(it)}
    }
    private fun <T> registryOperation(stage:String,operation:()->T):T {
        registryDiagnostic?.invoke("stage=$stage")
        try{return operation().also{registryDiagnostic?.invoke("stage=$stage result=ready")}}
        catch(failure:Exception){registryDiagnostic?.invoke("stage=$stage reason="+recipientFailureCode(failure));throw failure}
    }
    private fun recipientAliasText(alias: ByteArray): String {
        checkOpen(); invalid(alias.size in 1..256)
        return text(alias)
    }
    fun recipientSupported(): Boolean {
        val supported=recipientOperation("recipient_supported") {checkOpen();recipient.supported()}
        if(!supported)recipientEvidence.record("stage=recipient_supported reason=RECIPIENT_KEY_UNSUPPORTED")
        return supported
    }
    fun recipientAlias(generation: Int): ByteArray = recipientOperation("recipient_alias") {checkOpen();recipient.alias(generation).toByteArray(Charsets.US_ASCII)}
    fun recipientExists(alias: ByteArray): Boolean = recipientOperation("recipient_exists") {recipient.exists(recipientAliasText(alias))}.also {
        recipientEvidence.record("stage=recipient_exists state="+if(it)"present" else "absent")
    }
    fun recipientCreate(alias: ByteArray): ByteArray = recipientOperation("recipient_create") {recipient.create(recipientAliasText(alias))}
    fun recipientPublic(alias: ByteArray): ByteArray = recipientOperation("recipient_public") {recipient.publicKey(recipientAliasText(alias))}
    fun recipientAgree(alias: ByteArray, point: ByteArray): ByteArray = recipient.agreeP256(recipientAliasText(alias), point)
    fun hpkeSeal(publicKey: ByteArray, info: ByteArray, aad: ByteArray, plaintext: ByteArray): ByteArray {
        checkOpen(); invalid(plaintext.size <= 1048495)
        return OpaqueRecipientHpke.seal(publicKey, info, aad, plaintext)
    }
    fun hpkeOpen(alias: ByteArray, expected: ByteArray, enc: ByteArray, info: ByteArray, aad: ByteArray, ciphertext: ByteArray): ByteArray =
        AndroidRecipientHpke.open(recipient, recipientAliasText(alias), expected, enc, info, aad, ciphertext)
    fun createPeerTls(incoming: Boolean, expectedPeer: ByteArray): com.peerchat.android.platform.tls.PeerTls {
        checkOpen()
        invalid(expectedPeer.size == 64 && expectedPeer.all { it in 48..57 || it in 97..102 })
        return com.peerchat.android.platform.tls.PeerTls(
            com.peerchat.android.platform.tls.AndroidTlsIdentity(identity), incoming,
            expectedPeer.toString(Charsets.US_ASCII))
    }
    fun protect(bytes: ByteArray, context: ByteArray): ByteArray =
        if(context.toString(Charsets.UTF_8).startsWith("PeerChat/device-key-registry/v1/"))
            recipientOperation("registry_record_protect"){records.protect(bytes,context)} else records.protect(bytes,context)
    fun unprotect(bytes: ByteArray, context: ByteArray): ByteArray =
        if(context.toString(Charsets.UTF_8).startsWith("PeerChat/device-key-registry/v1/"))
            recipientOperation("registry_record_unprotect"){records.unprotect(bytes,context)} else records.unprotect(bytes,context)
    fun execute(bytes: ByteArray) = registryOperation("registry_execute") {
        checkOpen(); invalid(bytes.size <= 32768)
        val sql = text(bytes)
        // Native execute uses fixed schema/PRAGMA/transaction statements only;
        // all values use bound statements. Reject quoted literals/comments here
        // rather than ambiguously splitting arbitrary SQL at semicolons.
        nativeSqlStatements(sql).forEach { statement ->
            // Android rejects result-producing PRAGMAs in execSQL (including
            // journal_mode=WAL and secure_delete). Execute/drain the same native
            // statement via a cursor; do not change durability/schema policy.
            if (statement.startsWith("PRAGMA ", ignoreCase = true)) {
                database.rawQuery(statement, null).use { cursor -> while (cursor.moveToNext()) { } }
            } else database.execSQL(statement)
        }
    }
    fun prepare(bytes: ByteArray): Long = registryOperation("registry_prepare") {
        checkOpen(); invalid(bytes.size in 1..32768 && statements.size < 128)
        val sql = text(bytes); invalid(!sql.contains('\u0000') && !sql.contains(';'))
        if (nextStatement == Long.MAX_VALUE) throw ProviderFailure("STATEMENT_LIMIT")
        val id = nextStatement++; statements[id] = Statement(sql); id
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
    fun row(id: Long): Boolean = registryOperation("registry_row") {
        val s = statement(id)
        if (!s.started) {
            s.started = true
            if (s.sql.trimStart().startsWith("SELECT", ignoreCase = true)) {
                s.cursor = database.rawQueryWithFactory({ _, driver, editTable, query ->
                    bind(s, query); SQLiteCursor(driver, editTable, query)
                }, s.sql, emptyArray(), "")
            } else database.compileStatement(s.sql).use { bind(s, it); it.execute() }
        }
        s.cursor?.moveToNext() ?: false
    }
    private fun cursor(id: Long, index: Int): Cursor {
        val cursor = statement(id).cursor ?: throw ProviderFailure("NO_RESULT_ROW")
        invalid(!cursor.isBeforeFirst && !cursor.isAfterLast && index in 0 until cursor.columnCount && !cursor.isNull(index)); return cursor
    }
    fun bytes(id: Long, index: Int): ByteArray = registryOperation("registry_column_bytes") {cursor(id, index).getBlob(index).also { invalid(it.size in 1..1048576) }}
    fun text(id: Long, index: Int): ByteArray = cursor(id, index).getString(index).toByteArray(Charsets.UTF_8)
    fun number(id: Long, index: Int): Long = cursor(id, index).getLong(index)
    fun finish(id: Long) { statements.remove(id)?.cursor?.close() }
    override fun close() {
        if (!closed) { statements.values.forEach { it.cursor?.close() }; statements.clear(); database.close(); closed = true }
    }
}
