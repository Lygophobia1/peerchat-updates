package com.peerchat.android.platform

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.ByteArrayOutputStream
import java.io.File

/** A small first-frame preview. Metadata is re-encoded; source EXIF is never copied. */
internal fun attachmentThumbnail(file:File):ByteArray {
    try {
        val bounds=BitmapFactory.Options().apply{inJustDecodeBounds=true}
        BitmapFactory.decodeFile(file.path,bounds)
        if(bounds.outWidth !in 1..16384||bounds.outHeight !in 1..16384||bounds.outWidth.toLong()*bounds.outHeight>16L*1024*1024)return byteArrayOf()
        var sample=1
        while(maxOf(bounds.outWidth,bounds.outHeight)/sample>256)sample*=2
        val decoded=BitmapFactory.decodeFile(file.path,BitmapFactory.Options().apply{inSampleSize=sample})?:return byteArrayOf()
        try {
            for(edge in listOf(128,96,64,48)) {
                val scale=minOf(1.0,edge.toDouble()/maxOf(decoded.width,decoded.height))
                val image=Bitmap.createScaledBitmap(decoded,maxOf(1,(decoded.width*scale).toInt()),maxOf(1,(decoded.height*scale).toInt()),true)
                try {
                    for(quality in listOf(65,45,25)) {
                        val output=ByteArrayOutputStream()
                        if(image.compress(Bitmap.CompressFormat.JPEG,quality,output)) {
                            val bytes=output.toByteArray()
                            if(bytes.size<=2048)return bytes
                        }
                    }
                } finally {if(image!==decoded)image.recycle()}
            }
        } finally {decoded.recycle()}
    } catch(_:Exception) { /* Preview failure never blocks the original attachment. */ }
    return byteArrayOf()
}
