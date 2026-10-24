package com.peerchat.android

import android.content.Context
import android.graphics.Bitmap
import java.io.File
import java.security.MessageDigest

internal fun cachedPhotoFixture(context:Context,label:String):File {
    val file=File(File(context.cacheDir,"diagnostics").also{it.mkdirs()},"PeerChat-$label.png")
    val image=Bitmap.createBitmap(768,768,Bitmap.Config.ARGB_8888)
    try {
        val random=java.util.Random(214)
        val pixels=IntArray(768*768){0xff000000.toInt() or random.nextInt(0x1000000)}
        image.setPixels(pixels,0,768,0,0,768,768)
        android.graphics.Canvas(image).apply{
            drawRect(0f,0f,768f,120f,android.graphics.Paint().apply{color=0xff122433.toInt()})
            drawText("PeerChat encrypted photo",24f,76f,android.graphics.Paint().apply{color=android.graphics.Color.WHITE;textSize=38f;isAntiAlias=true})
        }
        file.outputStream().use{check(image.compress(Bitmap.CompressFormat.PNG,100,it))}
    } finally {image.recycle()}
    return file
}
internal fun cachedPhotoDigest(file:File):ByteArray {
    val hash=MessageDigest.getInstance("SHA-256")
    file.inputStream().use{input->val bytes=ByteArray(32768);while(true){val count=input.read(bytes);if(count<0)break;if(count>0)hash.update(bytes,0,count)}}
    return hash.digest()
}
