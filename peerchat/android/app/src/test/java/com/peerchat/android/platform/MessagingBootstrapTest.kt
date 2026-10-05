package com.peerchat.android.platform

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[28])
class MessagingBootstrapTest {
    private val device="a".repeat(64)
    private fun response():JSONObject=JSONObject().put("version",1).put("bootstrapVersion",1)
        .put("accountId",8).put("registeredPublicIdentity",device).put("presenceLive",true).put("relayOnlyPresence",true)
        .put("friendState",JSONObject().put("friends",JSONArray()).put("incoming",JSONArray()).put("outgoing",JSONArray()).put("blocked",JSONArray()))
    private fun rejects(value:JSONObject) {
        var rejected=false
        try{validateMessagingBootstrap(value,8,device)}catch(_:Exception){rejected=true}
        assertTrue("Invalid bootstrap installed",rejected)
    }
    @Test fun validAuthoritativeStateAcceptedWithoutUi() {
        val result=response()
        assertSame(result.getJSONObject("friendState"),validateMessagingBootstrap(result,8,device))
    }
    @Test fun accountDeviceVersionAndLeaseFailuresFailClosed() {
        rejects(response().put("accountId",9));rejects(response().put("registeredPublicIdentity","b".repeat(64)))
        rejects(response().put("bootstrapVersion",2));rejects(response().put("version",2))
        rejects(response().put("presenceLive",false));rejects(response().put("relayOnlyPresence",false))
    }
    @Test fun missingPartialMalformedAndOversizedStateRejected() {
        val missing=response();missing.remove("friendState");rejects(missing)
        val partial=response();partial.getJSONObject("friendState").remove("blocked");rejects(partial)
        rejects(response().put("friendState","malformed"))
        val large=response();val rows=JSONArray();repeat(129){rows.put(JSONObject())}
        large.getJSONObject("friendState").put("friends",rows);rejects(large)
    }
}
