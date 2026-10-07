package com.peerchat.android.platform

/** Server trust only; blank edits cannot erase a trusted endpoint's pin. */
internal fun sameServer(a: ServerEndpoint, b: ServerEndpoint) =
    a.host.trim().equals(b.host.trim(), ignoreCase = true) && a.port == b.port

enum class ServerTrustState { Untrusted, Trusted, CertificateChanged }
fun serverTrustState(draft:ServerEndpoint,saved:ServerEndpoint?,changed:ServerEndpoint?):ServerTrustState =
    when {
        changed!=null&&sameServer(draft,changed)->ServerTrustState.CertificateChanged
        saved!=null&&sameServer(draft,saved)&&saved.certificatePin.matches(Regex("[0-9a-fA-F]{64}"))->ServerTrustState.Trusted
        else->ServerTrustState.Untrusted
    }

internal fun serverTrustExpected(saved: ServerEndpoint?, draft: ServerEndpoint): String {
    if (saved != null && sameServer(saved, draft) && saved.certificatePin.isNotEmpty()) {
        require(draft.certificatePin.isEmpty() ||
            draft.certificatePin.equals(saved.certificatePin, ignoreCase = true)) {
            "Remove/reconfigure the trusted server explicitly before replacing its identity."
        }
        return saved.certificatePin.lowercase()
    }
    return draft.certificatePin.lowercase()
}

internal fun validateObservedServer(expected: String, observed: String) {
    require(observed.matches(Regex("[0-9a-f]{64}")))
    require(expected.isEmpty() || expected == observed) { "Server identity changed" }
}
