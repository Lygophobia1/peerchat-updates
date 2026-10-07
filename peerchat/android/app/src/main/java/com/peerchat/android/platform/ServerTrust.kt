package com.peerchat.android.platform

/** Server trust only; blank edits cannot erase a trusted endpoint's pin. */
internal fun sameServer(a: ServerEndpoint, b: ServerEndpoint) =
    a.host.equals(b.host, ignoreCase = true) && a.port == b.port

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
