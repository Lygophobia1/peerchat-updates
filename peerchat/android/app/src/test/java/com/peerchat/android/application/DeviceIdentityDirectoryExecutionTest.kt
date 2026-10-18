package com.peerchat.android.application

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[28])
class DeviceIdentityDirectoryExecutionTest {
    private val pin="a".repeat(64)
    private fun response(account:Long=9)=JSONObject().put("deviceIdentityVersion",1)
        .put("serverFingerprint",pin).put("accountId",account.toString()).put("revision",3)
        .put("authorityProof","AQID").put("authorityActive",true)
        .put("endorsements",JSONArray().put(JSONObject().put("proof","BAUG").put("active",true)))
        .put("possessionProofs",JSONArray())

    @Test fun snapshotIsBoundedAndAccountPinned(){
        val value=DeviceIdentityDirectoryExecution.snapshot(response(),9,pin,1)
        assertEquals(3L,value.revision)
        assertArrayEquals(byteArrayOf(1,2,3),value.authorityProof)
        assertEquals(9L,DeviceIdentityDirectoryExecution.snapshot(response().put("accountId",9L),9,pin,1).account)
        assertThrows(IllegalArgumentException::class.java){DeviceIdentityDirectoryExecution.snapshot(response(),10,pin,1)}
        assertThrows(IllegalArgumentException::class.java){DeviceIdentityDirectoryExecution.snapshot(response().put("serverFingerprint","b".repeat(64)),9,pin,1)}
        assertThrows(IllegalArgumentException::class.java){DeviceIdentityDirectoryExecution.snapshot(response().put("possessionProofs",JSONArray().put("AQID")),9,pin,1)}
        assertThrows(IllegalArgumentException::class.java){DeviceIdentityDirectoryExecution.snapshot(response().put("endorsements",JSONArray().apply{repeat(33){put(JSONObject().put("proof","AQID").put("active",true))}}),9,pin,1)}
    }

    @Test fun nativeCommandHasExactFieldsAndApprovalIsExplicit(){
        val command=DeviceIdentityDirectoryExecution.command(2,1,7,pin,123){it.blob(byteArrayOf(5));it.text("b".repeat(64))}
        val record=NativeRecord(command,30)
        assertEquals("PeerChat/IdentityDirectoryCommand/v1",record.text())
        assertEquals(2L,record.number());assertEquals(1L,record.number());assertEquals(7L,record.number())
        assertEquals(pin,record.text());assertEquals(123L,record.number())
        assertArrayEquals(byteArrayOf(5),record.bytes());assertEquals("b".repeat(64),record.text());record.end()
        var reached=false
        assertThrows(IllegalArgumentException::class.java){DeviceIdentityDirectoryExecution.apply({reached=true;byteArrayOf(1)},
            DeviceIdentityDirectoryExecution.snapshot(response().put("authorityProof","" ).put("authorityActive",false),9,pin,1),1,7,123)}
        assertFalse(reached)
    }
    @Test fun unverifiedAccountDoesNotStopAnotherSnapshot(){
        val unverified=DeviceIdentityDirectoryExecution.snapshot(response(9),9,pin,1)
        val verified=DeviceIdentityDirectoryExecution.snapshot(response(10),10,pin,1)
        val visited=mutableListOf<Long>()
        listOf(unverified,verified).forEach{snapshot->
            val applied=DeviceIdentityDirectoryExecution.applyIfVerified({
                visited+=snapshot.account
                if(snapshot.account==9L)byteArrayOf(0)else byteArrayOf(1)
            },snapshot,1,7,123)
            assertEquals(snapshot.account==10L,applied)
        }
        assertEquals(listOf(9L,10L),visited)
    }
}
