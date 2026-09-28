package com.slacodec.app.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.util.LruCache
import com.slacodec.app.model.Track

fun scaleMaxBitmap(b: Bitmap, max: Int): Bitmap {
    val largest = maxOf(b.width, b.height)
    if (largest <= max) return b
    val scale = max.toFloat() / largest
    return Bitmap.createScaledBitmap(
        b,
        (b.width * scale).toInt().coerceAtLeast(1),
        (b.height * scale).toInt().coerceAtLeast(1),
        true
    )
}

object ArtLoader {
    private val cache = LruCache<String, Bitmap>(80)
    fun get(key: String): Bitmap? = cache.get(key)
    fun load(context: Context, track: Track): Bitmap? {
        cache.get(track.path)?.let { return it }
        if (track.isSlac) return null
        val bmp = runCatching {
            val r = MediaMetadataRetriever()
            r.setDataSource(context, Uri.parse(track.path))
            val bytes = r.embeddedPicture
            r.release()
            bytes?.let { BitmapFactory.decodeByteArray(it, 0, it.size) }
                ?.let { scaleMaxBitmap(it, 120) }
        }.getOrNull()
        bmp?.let { cache.put(track.path, it) }
        return bmp
    }
}
