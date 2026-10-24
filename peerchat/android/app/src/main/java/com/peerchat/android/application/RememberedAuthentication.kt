package com.peerchat.android.application

/** Retry transport unavailability only. Certificate/identity failures never
 * become a reconnect loop and never remove the protected credential. */
internal fun isRememberedNetworkFailure(failure:Exception):Boolean =
    failure is java.net.SocketException || failure is java.net.SocketTimeoutException ||
        failure is java.net.UnknownHostException || failure is java.io.EOFException

internal fun isRememberedCredentialRejected(failure:Exception):Boolean =
    failure is com.peerchat.android.platform.ServerRejected && failure.code in
        setOf("remember_revoked","remember_invalid","remember_proof_invalid")

/** Fixed categories only; never expose exception text or discard saved sign-in. */
internal fun rememberedFailureStatus(failure:Exception):String {
    var cause:Throwable?=failure
    repeat(16){
        if(cause is java.security.cert.CertificateExpiredException)
            return "Server certificate expired. Your saved sign-in is retained; the server administrator must renew it."
        if(cause is java.security.cert.CertificateNotYetValidException)
            return "Server certificate is not valid yet. Check the device time; your saved sign-in is retained."
        cause=cause?.cause
    }
    if(failure is javax.net.ssl.SSLException || failure is java.security.cert.CertificateException)
        return "Server identity could not be verified. Your saved sign-in is retained; verify Server Settings."
    return "Saved authentication failed; sign in again"
}
