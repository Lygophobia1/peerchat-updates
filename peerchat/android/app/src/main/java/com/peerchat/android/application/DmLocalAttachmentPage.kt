package com.peerchat.android.application

/** Disposable display page only. Protected native jobs remain the sole source
 * of custody, scope, acceptance, download and acknowledgement authority. */
internal data class DmLocalAttachmentPage(val rows:List<AttachmentSnapshot>,val cursor:String,val more:Boolean)
internal fun dmLocalAttachmentPage(bytes:ByteArray):DmLocalAttachmentPage {
    val r=NativeRecord(bytes,30);require(r.text()=="PeerChat/DMLocalAttachmentPage/v1")
    val cursor=r.text();val more=r.number().also{require(it in 0..1)}==1L
    require(cursor.length<=68&&(cursor.isEmpty()||cursor.matches(Regex("[0-9a-f]{64}:(?:0|[1-9][0-9]{0,2})")))&&more==cursor.isNotEmpty())
    val rows=attachmentSnapshots(r.bytes());r.end();require(rows.size<=16)
    return DmLocalAttachmentPage(rows,cursor,more)
}
