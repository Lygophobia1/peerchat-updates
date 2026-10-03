package com.peerchat.android.platform

import android.content.Context
import org.json.JSONObject

/** Configuration only: never passwords, session tokens or private identities. */
interface ServerConfigurationRepository {
    fun load(): ServerEndpoint?
    fun save(endpoint: ServerEndpoint)
}

internal fun validStoredServer(endpoint: ServerEndpoint): Boolean =
    endpoint.host.isNotEmpty() && endpoint.host.length <= 253 &&
        endpoint.host.none { it.isWhitespace() || it == '/' || it == '\\' } &&
        endpoint.port in 1..65535 &&
        (endpoint.certificatePin.isEmpty() || endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))

/** PCR1 authenticates host, port and pin as one record. Corruption never becomes
 * an empty pin or a default endpoint. The same existing profile record key is used. */
internal class ProtectedServerConfigurationStore(
    private val read: () -> ByteArray?,
    private val write: (ByteArray) -> Unit,
    private val protector: () -> RecordProtector
) : ServerConfigurationRepository {
    private val label = "server-configuration-v1".toByteArray(Charsets.UTF_8)
    override fun load(): ServerEndpoint? {
        val blob = read() ?: return null
        require(blob.size in 1..4096)
        val plain = protector().unprotect(blob, label)
        require(plain.size in 1..2048)
        val json = JSONObject(plain.toString(Charsets.UTF_8))
        require(json.length() == 4 && json.getInt("version") == 1)
        return ServerEndpoint(json.getString("host"), json.getInt("port"), json.getString("pin"))
            .also { require(validStoredServer(it)) }
    }
    override fun save(endpoint: ServerEndpoint) {
        val normalized = endpoint.copy(host = endpoint.host.trim(), certificatePin = endpoint.certificatePin.lowercase())
        require(validStoredServer(normalized))
        val plain = JSONObject().put("version", 1).put("host", normalized.host)
            .put("port", normalized.port).put("pin", normalized.certificatePin)
            .toString().toByteArray(Charsets.UTF_8)
        write(protector().protect(plain, label))
    }
}

class AndroidServerConfigurationStore(context: Context, profile: String = "default") : ServerConfigurationRepository {
    private val state = ProfileSecurity(context.applicationContext, profile)
    private val records by lazy { AndroidRecordProtector(context.applicationContext, profile) }
    private val delegate = ProtectedServerConfigurationStore(
        { state.locked { state.read("server-configuration.pcr") } },
        { bytes -> state.locked { state.write("server-configuration.pcr", bytes) } },
        { records }
    )
    override fun load() = delegate.load()
    override fun save(endpoint: ServerEndpoint) = delegate.save(endpoint)
}
