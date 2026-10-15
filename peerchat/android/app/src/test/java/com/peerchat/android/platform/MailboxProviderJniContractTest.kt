package com.peerchat.android.platform

import org.junit.Assert.assertEquals
import org.junit.Test

/** Checks the JVM descriptors looked up by android_storage_provider.hpp and
 * mailboxProviders without opening a profile, SQLite file, or Keystore key. */
class MailboxProviderJniContractTest {
    @Test fun registryInitializationMethodsMatchNativeDescriptors() {
        val type = AndroidStoragePlatform::class.java
        fun method(name: String, result: Class<*>, vararg arguments: Class<*>) {
            assertEquals(name, result, type.getMethod(name, *arguments).returnType)
        }
        val bytes = ByteArray::class.java
        val long = java.lang.Long.TYPE
        val int = Integer.TYPE
        val boolean = java.lang.Boolean.TYPE
        val void = java.lang.Void.TYPE
        method("beginMailboxRegistration", void)
        method("keyRegistryProvider", Any::class.java)
        method("execute", void, bytes)
        method("prepare", long, bytes)
        method("bindBytes", void, long, int, bytes, boolean)
        method("bindNumber", void, long, int, long)
        method("row", boolean, long)
        method("bytes", bytes, long, int)
        method("text", bytes, long, int)
        method("number", long, long, int)
        method("finish", void, long)
        method("close", void)
        method("publicKey", bytes)
        method("protect", bytes, bytes, bytes)
        method("unprotect", bytes, bytes, bytes)
        method("recipientSupported", boolean)
        method("recipientAlias", bytes, int)
        method("recipientExists", boolean, bytes)
        method("recipientCreate", bytes, bytes)
        method("recipientPublic", bytes, bytes)
    }
}
