package com.peerchat.android.application

/** Retry transport unavailability only. Certificate/identity failures never
 * become a reconnect loop and never remove the protected credential. */
internal fun isRememberedNetworkFailure(failure:Exception):Boolean =
    failure is java.net.SocketException || failure is java.net.SocketTimeoutException ||
        failure is java.net.UnknownHostException || failure is java.io.EOFException

internal fun isRememberedCredentialRejected(failure:Exception):Boolean =
    failure is com.peerchat.android.platform.ServerRejected && failure.code in
        setOf("remember_revoked","remember_invalid","remember_proof_invalid")
