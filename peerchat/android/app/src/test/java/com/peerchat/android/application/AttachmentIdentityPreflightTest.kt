package com.peerchat.android.application
import org.junit.Assert.*
import org.junit.Test
import org.json.JSONObject
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class AttachmentIdentityPreflightTest {
 @Test fun missingIdentityBlocksWithoutPromotingTrust(){
  val missing="a".repeat(64)
  val reply=NativeRecordWriter(30).also{it.number(1);it.text(missing)}.bytes()
  try{requireCachedRecipientIdentities(reply);fail()}catch(e:AttachmentRecipientVerificationRequired){assertEquals(listOf(missing),e.devices)}
  requireCachedRecipientIdentities(NativeRecordWriter(30).also{it.number(0)}.bytes())
 }
 @Test fun audienceIsBoundedAndCanonical(){
  assertArrayEquals(byteArrayOf(0),cachedAudience(JSONObject().put("dataEncoding","base64url").put("audience","AA")))
  for(wire in listOf("AA==","AB","", "A".repeat(43692))){
   try{cachedAudience(JSONObject().put("dataEncoding","base64url").put("audience",wire));fail()}catch(_:IllegalArgumentException){}
  }
 }
 @Test fun malformedNativeIdentityReplyFailsClosed(){
  for(reply in listOf(NativeRecordWriter(30).also{it.number(33)}.bytes(),NativeRecordWriter(30).also{it.number(1);it.text("bad")}.bytes())){
   try{requireCachedRecipientIdentities(reply);fail()}catch(_:IllegalArgumentException){}
  }
 }
}
