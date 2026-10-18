package com.peerchat.android.platform

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[28])
class MediaTurnCredentialsTest {
    private val now=2000000000L
    private fun response()=JSONObject().put("ok",true).put("version",1).put("host","turn.example.invalid")
        .put("port",3478).put("transport","udp").put("username","synthetic:user").put("password","synthetic/credential+=")
        .put("expiresUtc",now+600)
    @Test fun existingResponsePreservesValuesWithoutLoggingCredentials(){
        val credentials=parseMediaTurnCredentials(response(),now)
        assertEquals("turn.example.invalid",credentials.host);assertEquals(3478,credentials.port)
        assertEquals("synthetic:user",credentials.username);assertEquals("synthetic/credential+=",credentials.password)
        assertTrue(credentials.usableAt(now+569));assertFalse(credentials.usableAt(now+570))
        assertFalse(credentials.toString().contains(credentials.username));assertFalse(credentials.toString().contains(credentials.password))
    }
    @Test fun malformedMissingAndExpiredValuesAreRejectedWithoutSecretErrors(){
        val cases=listOf("ok" to false,"version" to 2,"version" to "1","transport" to "tcp","port" to 0,
            "port" to 65536,"port" to 3478.5,"port" to "3478","expiresUtc" to now+30,"expiresUtc" to now-1,
            "host" to "turn.invalid/path","host" to "user@turn.invalid","host" to "a?transport=tcp",
            "host" to "a".repeat(254),"username" to "a".repeat(1025),"password" to "a".repeat(4097),
            "username" to "synthetic\nuser","password" to "", "password" to JSONObject.NULL)
        cases.forEach{(field,bad)->
            val error=assertThrows(IllegalArgumentException::class.java){parseMediaTurnCredentials(response().put(field,bad),now)}
            assertFalse(error.message.orEmpty().contains("synthetic/credential"))
        }
        for(field in listOf("host","port","username","password","transport","expiresUtc","version","ok")){
            val value=response();value.remove(field)
            assertThrows(IllegalArgumentException::class.java){parseMediaTurnCredentials(value,now)}
        }
    }
    @Test fun hostFormsAndLifetimeBoundsRemainCompatible(){
        for(host in listOf("192.0.2.1","[2001:db8::1]","turn.example.invalid"))assertEquals(host,parseMediaTurnCredentials(response().put("host",host),now).host)
        assertThrows(IllegalArgumentException::class.java){parseMediaTurnCredentials(response(),-1)}
        assertThrows(IllegalArgumentException::class.java){parseMediaTurnCredentials(response(),Long.MAX_VALUE)}
        assertFalse(parseMediaTurnCredentials(response(),now).usableAt(Long.MAX_VALUE))
    }
}
