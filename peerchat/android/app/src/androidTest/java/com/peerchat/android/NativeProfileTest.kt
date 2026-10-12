package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class NativeProfileTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private fun clean(profile: String) {
        check(profile.startsWith("test_"))
        val state = ProfileSecurity(context, profile)
        val store = state.store(); store.deleteEntry(state.identityAlias); store.deleteEntry(state.storageAlias)
        val directory = File(context.noBackupFilesDir, "profiles/" + state.id.joinToString("") { "%02x".format(it) })
        check(!directory.exists() || directory.deleteRecursively())
        check(!state.directory.exists() || state.directory.deleteRecursively())
    }
    @Test fun sqliteStartupPragmas() {
        val profile = "test_${UUID.randomUUID()}"
        try {
            AndroidStoragePlatform(context, profile).use {
                // Exact native Storage startup SQL; no records/secrets involved.
                it.execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA temp_store=MEMORY; PRAGMA secure_delete=ON;".toByteArray())
                fun scalar(sql: String): String {
                    val statement = it.prepare(sql.toByteArray())
                    try { assertTrue(it.row(statement)); return it.text(statement, 0).toString(Charsets.UTF_8) }
                    finally { it.finish(statement) }
                }
                assertEquals("wal", scalar("SELECT journal_mode FROM pragma_journal_mode"))
                assertEquals("2", scalar("SELECT synchronous FROM pragma_synchronous"))
                assertEquals("2", scalar("SELECT temp_store FROM pragma_temp_store"))
                assertEquals("1", scalar("SELECT secure_delete FROM pragma_secure_delete"))
            }
        } finally { clean(profile) }
    }
    @Test fun nativeAttachmentFilesAndRestart() {
        val profile = "test_${UUID.randomUUID()}"
        try {
            NativeProfile(context, profile).use { assertEquals(0, ProfileBridge.attachmentTest(it.handle)) }
        } finally { clean(profile) }
    }
    @Test fun crossProcessStorageCheckpoint() {
        val args = InstrumentationRegistry.getArguments()
        val phase = args.getString("storage_phase")
        org.junit.Assume.assumeTrue("Explicit cross-process driver required", phase != null)
        val profile = checkNotNull(args.getString("storage_profile"))
        check(profile.matches(Regex("test_restart_[a-f0-9]{32}")))
        if (phase == "cleanup") { clean(profile); return }
        val proof = File(ProfileSecurity(context, profile).directory, "restart.expected.public")
        NativeProfile(context, profile).use { native ->
            val public = checkNotNull(ProfileBridge.publicKey(native.handle))
            if (phase == "seed") {
                check(!proof.exists()); proof.writeBytes(public)
                assertEquals(0, ProfileBridge.storageTest(native.handle, 0))
            } else {
                assertArrayEquals(proof.readBytes(), public)
                val mode = when (phase) { "pending" -> 0; "ack" -> 1; "verify" -> 2; else -> error("Unknown phase") }
                assertEquals(0, ProfileBridge.storageTest(native.handle, mode))
            }
        }
    }
    @Test fun nativeProfileAndProductionStorage() {
        val profile = "test_${UUID.randomUUID()}"
        try {
            val public = AndroidIdentityProvider(context, profile).publicRepresentation()
            repeat(32) {
                val handle: Long
                NativeProfile(context, profile).use { native ->
                    handle = native.handle
                    assertArrayEquals(public, ProfileBridge.publicKey(handle))
                    assertArrayEquals(intArrayOf(1, 1, 1, 1, 1), NativeBridge.info(handle))
                    val bytes = "日本語 😀\u0000native signing".toByteArray()
                    assertTrue(PeerIdentityFormat.verify(public, bytes, checkNotNull(ProfileBridge.sign(handle, bytes))))
                    assertEquals(0, ProfileBridge.storageTest(handle, 0))
                    // Native local history works before any authentication,
                    // friend snapshot, presence lease, or peer TLS exists.
                    val peer="b".repeat(64).toByteArray(Charsets.US_ASCII)
                    val history=com.peerchat.android.application.messageSnapshots(checkNotNull(ReliableBridge.history(handle,peer,Long.MAX_VALUE)))
                    assertEquals(1,history.size)
                    assertFalse(com.peerchat.android.application.connectionSnapshot(checkNotNull(ReliableBridge.state(handle,peer))).ready)
                    assertTrue(com.peerchat.android.application.relayExecutions(checkNotNull(ReliableBridge.requests(handle))).isEmpty())
                    // An unverified identity cannot be used for offline enqueue.
                    assertNull(ReliableBridge.queueLocal(handle,peer,"Test only".toByteArray()))
                    assertNull(ProfileBridge.sign(handle, ByteArray(70001)))
                }
                assertNull(ProfileBridge.publicKey(handle)); assertNull(ProfileBridge.sign(handle, byteArrayOf(1)))
            }
            NativeProfile(context, profile).use { assertEquals(0, ProfileBridge.storageTest(it.handle, 1)) }
            NativeProfile(context, profile).use { assertEquals(0, ProfileBridge.storageTest(it.handle, 2)); assertEquals(0, ProfileBridge.storageTest(it.handle, 3)) }
            // Corrupted message must not prevent reading an independent valid profile.
            NativeProfile(context, profile).use { assertArrayEquals(public, ProfileBridge.publicKey(it.handle)) }
            val state = ProfileSecurity(context, profile)
            state.store().deleteEntry(state.identityAlias)
            try { NativeProfile(context, profile).close(); fail("Missing identity was regenerated") } catch (_: Exception) { }
            assertFalse(state.store().containsAlias(state.identityAlias))
        } finally { clean(profile) }
    }
    @Test fun existingNormalIdentityIsNotReplaced() {
        val expected = AndroidIdentityProvider(context, "default").publicRepresentation()
        NativeProfile(context).use {
            assertArrayEquals(expected, ProfileBridge.publicKey(it.handle))
            assertEquals(1, NativeBridge.info(it.handle)?.get(2))
            assertEquals(1, NativeBridge.info(it.handle)?.get(3))
            assertEquals(1, ProfileBridge.storageTest(it.handle, 0)) // test-only hook rejects production profile
            val payload = "PeerChat deterministic restart proof / 日本語\u0000😀".toByteArray()
            val signature = checkNotNull(ProfileBridge.sign(it.handle, payload))
            assertTrue(PeerIdentityFormat.verify(expected, payload, signature))
            // Public/signature only, for the unchanged Windows compatibility verifier.
            val proof = File(context.filesDir, "provider-proof").also { check(it.isDirectory || it.mkdirs()) }
            File(proof, "native-compatibility.bin").writeBytes(expected + signature)
        }
        assertArrayEquals(expected, AndroidIdentityProvider(context, "default").publicRepresentation())
    }
    @Test fun localTransferAdapterReopensAndRejectsUnauthenticatedOffers(){
        val name="test_${UUID.randomUUID()}"
        fun record()=com.peerchat.android.application.NativeRecordWriter(30).bytes()
        var path=""
        try{
            repeat(16){
                NativeProfile(context,name).use{native->
                    val r=com.peerchat.android.application.NativeRecord(checkNotNull(TransferBridge.request(native.handle,0,record())),30)
                    val current=r.text();r.end()
                    if(path.isNotEmpty())assertEquals(path,current);path=current
                    assertTrue(File(path).isDirectory)
                    assertTrue(com.peerchat.android.application.attachmentSnapshots(checkNotNull(TransferBridge.request(native.handle,4,record()))).isEmpty())
                    val offer=com.peerchat.android.application.NativeRecordWriter(30).also{it.text("a".repeat(64));it.text("../escape");it.text("file.png")}.bytes()
                    assertNull(TransferBridge.request(native.handle,1,offer))
                    assertNull(TransferBridge.request(native.handle,0,ByteArray(4097)))
                }
            }
            assertNull(TransferBridge.request(0,0,record()))
        }finally{clean(name)}
    }
}
