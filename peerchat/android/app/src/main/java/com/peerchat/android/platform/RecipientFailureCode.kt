package com.peerchat.android.platform

// JNI may clear a provider exception. Retain only an allowlisted classification,
// never its arbitrary message, alias, key bytes, or protected record contents.
internal fun recipientFailureCode(failure:Exception):String {
    val code=(failure as? ProviderFailure)?.code
    return if(code in setOf("RECIPIENT_KEY_UNSUPPORTED","RECIPIENT_KEY_ALREADY_EXISTS",
        "RECIPIENT_KEY_RECOVERY_REQUIRED","RECIPIENT_KEY_POLICY_MISMATCH","RECIPIENT_AGREEMENT_FAILED",
        "CRYPTO_PROVIDER_FAILURE","STORAGE_CLOSED","INVALID_ARGUMENT"))checkNotNull(code)
    else "RECIPIENT_PROVIDER_OPERATION_FAILED"
}
