package com.peerchat.android.platform

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class FriendStateOptionalNamesTest {
    @Test fun optInUsesFreshSnapshotAndOnlyOptionalMetadataErrorsRetryLegacy(){
        val result=JSONObject().put("fresh",true)
        assertSame(result,friendStateWithOptionalNames{assertEquals(1,it.getInt("deviceNamesVersion"));result})
        for(code in listOf("device_names_response_too_large","device_names_unavailable")){
            var attempts=0
            assertSame(result,friendStateWithOptionalNames{fields->
                if(attempts++==0){assertEquals(1,fields.getInt("deviceNamesVersion"));throw ServerRejected(code)}
                assertFalse(fields.has("deviceNamesVersion"));result
            });assertEquals(2,attempts)
        }
    }
    @Test fun authenticationAndNetworkFailuresNeverDowngrade(){
        for(code in listOf("session_invalid","device_revoked","unauthorized","unsupported_request")){
            var attempts=0
            assertThrows(ServerRejected::class.java){friendStateWithOptionalNames{attempts++;throw ServerRejected(code)}}
            assertEquals(1,attempts)
        }
        var attempts=0
        assertThrows(java.io.IOException::class.java){friendStateWithOptionalNames{attempts++;throw java.io.IOException()}}
        assertEquals(1,attempts)
    }
}
