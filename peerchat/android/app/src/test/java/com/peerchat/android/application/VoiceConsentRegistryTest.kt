package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class VoiceConsentRegistryTest {
    @Test fun preparationPriorityIsBoundedAndEndsOnCompletionCancellationOrReset(){
        var now=0L;val registry=VoiceConsentRegistry<Any>{now}
        assertFalse(registry.preparing())
        val first=registry.create("a".repeat(64),"1".repeat(32),true,Any())
        assertTrue(registry.preparing());registry.claim(first.token);assertTrue(registry.preparing())
        registry.clear(first.token);assertFalse(registry.preparing())
        registry.create("a".repeat(64),"2".repeat(32),false,Any())
        now=60_000_000_000L;assertFalse(registry.preparing())
        registry.create("a".repeat(64),"3".repeat(32),false,Any())
        assertTrue(registry.preparing());registry.reset();assertFalse(registry.preparing())
    }
    @Test fun preparingCallSurvivesUiAndWakeLeaseChangesWhileItsOwnLeaseRemains(){
        val gate=Any();val registry=VoiceConsentRegistry<Any>{0L};val binding=Any()
        val consent=registry.create("a".repeat(64),"1".repeat(32),false,binding)
        registry.claim(consent.token)
        val voiceLease=Long.MAX_VALUE to true
        var leases:List<Pair<Long?,Boolean>> = listOf(null to false,voiceLease)
        val scope=registry.operationScope(gate,consent.token,binding){backendLeasePolicy(leases).peerWork}
        assertEquals("ready",scope.network{
            leases=listOf(voiceLease,1000L to false) // UI stops, bounded push work starts.
            scope.local{}
            leases=listOf(voiceLease) // Push completes without replacing the call.
            scope.local{}
            leases=listOf(null to false,voiceLease) // UI returns.
            "ready"
        })
        registry.clear(consent.token)
        assertThrows(ScopedBackendWork.Superseded::class.java){scope.local{fail("Ended call resumed")}}
    }
    @Test fun preparingCallRejectsLateResultsAfterAuthenticationReplacementOrConsentRelease(){
        for(change in listOf("account","generation","session","profile","stop","consent")){
            val gate=Any();val registry=VoiceConsentRegistry<Any>{0L};val binding=Any()
            val consent=registry.create("a".repeat(64),"1".repeat(32),false,binding)
            registry.claim(consent.token)
            var profile:Any=Any();val owner=Any();var session:Any=Any();var account=7L;var generation=11L;var allowed=true
            val identity=ScopedBackendWork.Identity(profile,owner,session,account,generation)
            val scope=registry.operationScope(gate,consent.token,binding){allowed&&identity.matches(profile,owner,session,account,generation)}
            assertThrows(change,ScopedBackendWork.Superseded::class.java){scope.network{
                when(change){
                    "account"->account++
                    "generation"->generation++
                    "session"->session=Any()
                    "profile"->profile=Any()
                    "stop"->allowed=false
                    "consent"->registry.clear(consent.token)
                }
                "late voice credentials"
            }}
            assertFalse(scope.finish{fail("Stale call applied")})
        }
    }

    private val peer = "a".repeat(64)
    private val id = "1".repeat(32)

    @Test fun validConsentCanBeClaimedOnceWithItsExactBinding() {
        var now = 10L
        val registry = VoiceConsentRegistry<Any> { now }
        val binding = Any()
        val consent = registry.create(peer, id, answer = false, binding = binding)

        assertTrue(registry.valid(consent.token))
        val claimed = registry.claim(consent.token)
        assertEquals(consent, claimed.first)
        assertSame(binding, claimed.second)
        assertTrue(registry.current(consent.token, binding))
    }

    @Test fun duplicateClaimIsRejected() {
        val registry = VoiceConsentRegistry<Any> { 0L }
        val consent = registry.create(peer, id, answer = true, binding = Any())

        registry.claim(consent.token)
        assertThrows(IllegalStateException::class.java) { registry.claim(consent.token) }
    }

    @Test fun consentExpiresAtSixtySecondsUsingMonotonicClock() {
        var now = 5_000_000L
        val registry = VoiceConsentRegistry<Any> { now }
        val consent = registry.create(peer, id, answer = false, binding = Any())
        val expiresAt = now + 60_000_000_000L

        now = expiresAt - 1
        assertTrue(registry.valid(consent.token))
        now = expiresAt
        assertFalse(registry.valid(consent.token))
        assertThrows(IllegalStateException::class.java) { registry.claim(consent.token) }
    }

    @Test fun staleTokenCannotClearReplacementConsent() {
        val registry = VoiceConsentRegistry<Any> { 0L }
        val old = registry.create(peer, id, answer = false, binding = Any())
        registry.clear(old.token)
        val replacement = registry.create("b".repeat(64), "2".repeat(32), answer = true, binding = Any())

        registry.clear(old.token)

        assertTrue(registry.valid(replacement.token))
        assertEquals(replacement, registry.claim(replacement.token).first)
    }

    @Test fun resetInvalidatesConsentAndAllowsANewOne() {
        val registry = VoiceConsentRegistry<Any> { 0L }
        val old = registry.create(peer, id, answer = false, binding = Any())

        registry.reset()

        assertFalse(registry.valid(old.token))
        assertThrows(IllegalStateException::class.java) { registry.claim(old.token) }
        val replacement = registry.create(peer, id, answer = true, binding = Any())
        assertTrue(registry.valid(replacement.token))
    }

    @Test fun currentRequiresTheSameBindingObject() {
        data class Binding(val account: String)

        val registry = VoiceConsentRegistry<Binding> { 0L }
        val binding = Binding("account")
        val equalButDistinct = Binding("account")
        val consent = registry.create(peer, id, answer = true, binding = binding)

        assertEquals(binding, equalButDistinct)
        assertNotSame(binding, equalButDistinct)
        assertTrue(registry.current(consent.token, binding))
        assertFalse(registry.current(consent.token, equalButDistinct))
    }

    @Test fun invalidPeerAndCallIdsAreRejected() {
        val registry = VoiceConsentRegistry<Any> { 0L }

        for (badPeer in listOf("", "a".repeat(63), "a".repeat(65), "A".repeat(64), "g".repeat(64))) {
            assertThrows(IllegalArgumentException::class.java) {
                registry.create(badPeer, id, answer = false, binding = Any())
            }
        }
        for (badId in listOf("", "1".repeat(31), "1".repeat(33), "A".repeat(32), "z".repeat(32))) {
            assertThrows(IllegalArgumentException::class.java) {
                registry.create(peer, badId, answer = false, binding = Any())
            }
        }

        assertTrue(registry.valid(registry.create(peer, id, answer = false, binding = Any()).token))
    }
}
