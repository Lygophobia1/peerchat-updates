package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.NativeRecord
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.application.attachmentSnapshots
import com.peerchat.android.application.deviceVerification
import com.peerchat.android.application.relayExecutions
import com.peerchat.android.application.verificationDecision
import com.peerchat.android.platform.AndroidRecipientKeyProvider
import com.peerchat.android.platform.PeerIdentityFormat
import com.peerchat.android.platform.ProfileSecurity
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.InetAddress
import java.net.Socket
import java.util.Base64
import java.util.UUID
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Test-only ADB-reverse control bridge. The device uses shipping JNI, Android
 * POSIX storage and Keystore; the bridge transports opaque native records only.
 * It never provisions a contact or substitutes for native TLS verification. */
@RunWith(AndroidJUnit4::class)
class NativeWindowsDmInteropTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private val encoder = Base64.getEncoder()
    private val decoder = Base64.getDecoder()
    private fun enc(value: ByteArray) = encoder.encodeToString(value)
    private fun dec(value: String) = decoder.decode(value)
    private fun record(vararg fields: String) = NativeRecordWriter(30).also { writer -> fields.forEach(writer::text) }.bytes()
    private fun peer(value: ByteArray) = PeerIdentityFormat.peerId(value)
    private fun pinRound(handle: Long, attempt: Long, peerId: String, previous: String?) {
        while (true) {
            val event = ReliableBridge.exchange(handle, attempt, ReliableBridge.EVENT, byteArrayOf()) ?: return
            if (event.isEmpty()) return
            val value = NativeRecord(event, 3)
            val kind = value.number()
            value.text(); value.text(); value.number()
            // Native event may contain a message record for receive events;
            // pin setup has no message, and all non-trust events are ignored.
            if (kind != 0L) continue
            val accepted = checkNotNull(ReliableBridge.exchange(handle, attempt,
                ReliableBridge.INITIAL_AUTHENTICATED_FRIEND_PIN, byteArrayOf()))
            if (accepted.firstOrNull() == 1.toByte()) continue
            val challenge = checkNotNull(ReliableBridge.exchange(handle, attempt,
                ReliableBridge.DEVICE_VERIFICATION, byteArrayOf()))
            val verification = deviceVerification(challenge, "isolated Windows fixture")
            assertEquals(1L, verification.localAccount)
            assertEquals(1L, verification.generation)
            assertEquals(2L, verification.account)
            assertEquals(peerId, verification.presented)
            assertNotNull("Only an existing verified device may authorize a second device", previous)
            assertTrue(verification.previous.contains(previous))
            assertArrayEquals(byteArrayOf(1), ReliableBridge.exchange(handle, attempt,
                ReliableBridge.VERIFY_PRESENTED_DEVICE, verificationDecision(verification, true)))
        }
    }

    @Test(timeout = 240_000)
    fun genuineAndroidToWindowsCacheFixture() {
        val arguments = InstrumentationRegistry.getArguments()
        val configured = arguments.getString("bridgePort")?.toIntOrNull()
        assumeTrue("Run only with an isolated ADB-reverse bridge", configured != null)
        val port = checkNotNull(configured)
        require(port in 1024..65535)
        val name = "test_windows_dm_${UUID.randomUUID().toString().replace("-", "")}".take(64)
        val security = ProfileSecurity(context, name)
        val aliases = setOf(AndroidRecipientKeyProvider(context, name).alias(1),
            AndroidRecipientKeyProvider(context, name).alias(2))
        try {
            NativeProfile(context, name).use { native ->
                val handle = native.handle
                val identity = checkNotNull(ProfileBridge.publicKey(handle))
                Socket(InetAddress.getByName("127.0.0.1"), port).use { socket ->
                    socket.soTimeout = 45_000
                    val input = BufferedReader(InputStreamReader(socket.getInputStream(), Charsets.US_ASCII))
                    val output = PrintWriter(socket.getOutputStream().writer(Charsets.US_ASCII), true)
                    fun send(value: ByteArray) = output.println(enc(value))
                    fun read() = dec(checkNotNull(input.readLine()) { "Windows fixture bridge closed" })
                    send(identity)
                    val setup = NativeRecord(read(), 30)
                    assertEquals("PeerChat/EmulatorDmSetup/v1", setup.text())
                    val windows = listOf(setup.bytes(), setup.bytes())
                    setup.end()
                    val peers = windows.map(::peer)
                    val friends = NativeRecordWriter(1).apply {
                        number(2)
                        peers.forEach { expected ->
                            number(2); text(expected); number(2); peers.forEach(::text)
                        }
                    }.bytes()
                    assertEquals(0, ReliableBridge.friends(handle, 1, 1, friends))
                    peers.forEachIndexed { index, id -> assertEquals("presence device $index", 0,
                        ReliableBridge.presence(handle,id.toByteArray(Charsets.US_ASCII),true,1,1)) }
                    assertEquals(0, MailboxBridge.configureEnrollment(handle, 1, 1,
                        "fixture.local:7443".toByteArray(), "f".repeat(64).toByteArray()))
                    val binding = checkNotNull(MailboxBridge.registration(handle, true))
                    send(binding)
                    val ready = NativeRecord(read(), 30)
                    assertEquals("PeerChat/EmulatorDmServer/v1", ready.text())
                    val serverPort = ready.number().toInt()
                    val pin = ready.text()
                    ready.end()
                    assertEquals(64, pin.length)
                    assertEquals(0, MailboxBridge.configureEnrollment(handle, 1, 1,
                        "localhost:$serverPort".toByteArray(), pin.toByteArray()))
                    assertEquals(0, MailboxBridge.registered(handle, binding))
                    val session = NativeRecordWriter(30).apply { number(1); number(1) }.bytes()
                    assertNotNull(TransferBridge.request(handle, 11, session))
                    val attempts = mutableMapOf<Int, Long>()
                    // Windows drives only bounded commands; all attachment and
                    // authentication state remains in this production profile.
                    while (true) {
                        val line = checkNotNull(input.readLine()) { "Windows fixture bridge closed" }
                        require(line.length <= 150_000)
                        val parts = line.split('|', limit = 4)
                        val response = when (parts[0]) {
                            "C" -> {
                                assertNotNull(ReliableBridge.requests(handle))
                                checkNotNull(TransferBridge.request(handle, 12, byteArrayOf()))
                            }
                            "R" -> byteArrayOf(if (TransferBridge.request(handle, 13, dec(parts[1])) != null) 1 else 0)
                            "I" -> IdentityDirectoryBridge.request(handle,dec(parts[1])) ?: byteArrayOf(0)
                            "P" -> checkNotNull(TransferBridge.request(handle, 11, session))
                            "V" -> checkNotNull(TransferBridge.request(handle, 4, record()))
                            "T" -> {
                                val index = parts[1].toInt()
                                require(index in 0..1 && index !in attempts)
                                val peerId = peers[index]
                                assertEquals(0, ReliableBridge.demand(handle, peerId.toByteArray()))
                                val pending = relayExecutions(checkNotNull(ReliableBridge.requests(handle)))
                                    .single { it.peerId == peerId }
                                assertEquals(pending.attempt, ReliableBridge.open(handle, pending.attempt,
                                    peer(identity) > peerId, peerId.toByteArray()))
                                attempts[index] = pending.attempt
                                checkNotNull(ReliableBridge.exchange(handle, pending.attempt,
                                    ReliableBridge.RECEIVE, byteArrayOf()))
                                byteArrayOf(1)
                            }
                            "X" -> {
                                val index = parts[1].toInt()
                                val attempt = checkNotNull(attempts[index])
                                val incoming = dec(parts[2])
                                if (incoming.isNotEmpty()) assertNotNull(ReliableBridge.exchange(handle,
                                    attempt, ReliableBridge.RECEIVE, incoming))
                                pinRound(handle, attempt, peers[index], if (index == 0) null else peers[0])
                                val outgoing = checkNotNull(ReliableBridge.exchange(handle,
                                    attempt, ReliableBridge.OUTPUT, byteArrayOf()))
                                val state = checkNotNull(ReliableBridge.exchange(handle,
                                    attempt, ReliableBridge.STATUS, byteArrayOf()))
                                NativeRecordWriter(30).apply {
                                    blob(outgoing); number(state[0].toLong()); number(state[1].toLong())
                                }.bytes()
                            }
                            "O" -> {
                                val directory = NativeRecord(checkNotNull(TransferBridge.request(handle, 0,
                                    record())), 30).text()
                                val filename = "${UUID.randomUUID().toString().replace("-", "")}-emulator-日本-📷.bin"
                                val bytes = ByteArray(256 * 1024 + 17) { index -> (index * 37 + 23).toByte() }
                                File(directory, filename).outputStream().use { stream -> stream.write(bytes); stream.flush() }
                                checkNotNull(TransferBridge.request(handle, 10,
                                    record(peers[0], filename, "emulator-日本-📷.bin")))
                            }
                            "N" -> MailboxBridge.receive(handle, dec(parts[1])) ?: byteArrayOf(0)
                            "A" -> checkNotNull(TransferBridge.request(handle, 2, record(parts[1])))
                            "K" -> checkNotNull(TransferBridge.request(handle, 3, record(parts[1])))
                            "D" -> {
                                val row = attachmentSnapshots(checkNotNull(TransferBridge.request(handle, 4,
                                    record()))).single { it.id == parts[1] }
                                assertTrue("Android cache import must be verified and committed", row.complete)
                                assertFalse(row.outgoing)
                                assertEquals("windows-original-日本-📷.bin", row.name)
                                val expected = ByteArray(256 * 1024 + 17) { index ->
                                    (index * 37 + 23).toByte()
                                }
                                assertArrayEquals(expected, File(row.localPath).readBytes())
                                byteArrayOf(1)
                            }
                            "S" -> {
                                val row = attachmentSnapshots(checkNotNull(TransferBridge.request(handle, 4,
                                    record()))).single { it.id == parts[1] }
                                assertTrue(row.outgoing)
                                assertEquals("emulator-日本-📷.bin", row.name)
                                byteArrayOf(if (row.cachePhase == "Recipients verified saved") 1 else 0)
                            }
                            "Q" -> { send(byteArrayOf(1)); break }
                            else -> error("Unknown Windows fixture control operation")
                        }
                        send(if (response.isEmpty()) byteArrayOf(0) else response)
                    }
                }
            }
        } finally {
            check(name.startsWith("test_windows_dm_"))
            val store = security.store()
            aliases.forEach(store::deleteEntry)
            store.deleteEntry(security.identityAlias)
            store.deleteEntry(security.storageAlias)
            val profile = File(context.noBackupFilesDir,
                "profiles/" + security.id.joinToString("") { "%02x".format(it) })
            check(!profile.exists() || profile.deleteRecursively())
            check(!security.directory.exists() || security.directory.deleteRecursively())
        }
    }
}
