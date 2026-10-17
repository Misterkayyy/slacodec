package com.slacodec.app.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.util.LruCache
import com.slacodec.app.library.SlacHeaderParser
import com.slacodec.app.model.Track
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext

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

/** Decodifica já reduzida (inSampleSize): bem mais barato que decodificar a capa inteira. */
fun decodeThumb(bytes: ByteArray, target: Int = 120): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
    var s = 1
    while (bounds.outWidth / (s * 2) >= target && bounds.outHeight / (s * 2) >= target) s *= 2
    val opts = BitmapFactory.Options().apply { inSampleSize = s }
    return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)?.let { scaleMaxBitmap(it, target) }
}

// Box blur real: funciona em QUALQUER versao do Android (sem RenderScript/Modifier.blur)
fun blurBitmap(src: Bitmap, radius: Int = 6, iterations: Int = 2): Bitmap {
    val bmp = src.copy(Bitmap.Config.ARGB_8888, true)
    var px = IntArray(bmp.width * bmp.height)
    bmp.getPixels(px, 0, bmp.width, 0, 0, bmp.width, bmp.height)
    repeat(iterations) { px = boxBlur(px, bmp.width, bmp.height, radius) }
    bmp.setPixels(px, 0, bmp.width, 0, 0, bmp.width, bmp.height)
    return bmp
}

private fun boxBlur(src: IntArray, w: Int, h: Int, r: Int): IntArray {
    val out = src.copyOf()
    val div = 2 * r + 1
    for (y in 0 until h) {
        var sa = 0; var sr = 0; var sg = 0; var sb = 0
        for (i in -r..r) {
            val c = src[y * w + i.coerceIn(0, w - 1)]
            sa += c ushr 24; sr += (c shr 16) and 0xFF; sg += (c shr 8) and 0xFF; sb += c and 0xFF
        }
        for (x in 0 until w) {
            out[y * w + x] = (sa / div shl 24) or (sr / div shl 16) or (sg / div shl 8) or (sb / div)
            val add = src[y * w + (x + r + 1).coerceAtMost(w - 1)]
            val rem = src[y * w + (x - r).coerceAtLeast(0)]
            sa += (add ushr 24) - (rem ushr 24)
            sr += ((add shr 16) and 0xFF) - ((rem shr 16) and 0xFF)
            sg += ((add shr 8) and 0xFF) - ((rem shr 8) and 0xFF)
            sb += (add and 0xFF) - (rem and 0xFF)
        }
    }
    val src2 = out.copyOf()
    for (x in 0 until w) {
        var sa = 0; var sr = 0; var sg = 0; var sb = 0
        for (i in -r..r) {
            val c = src2[i.coerceIn(0, h - 1) * w + x]
            sa += c ushr 24; sr += (c shr 16) and 0xFF; sg += (c shr 8) and 0xFF; sb += c and 0xFF
        }
        for (y in 0 until h) {
            out[y * w + x] = (sa / div shl 24) or (sr / div shl 16) or (sg / div shl 8) or (sb / div)
            val add = src2[(y + r + 1).coerceAtMost(h - 1) * w + x]
            val rem = src2[(y - r).coerceAtLeast(0) * w + x]
            sa += (add ushr 24) - (rem ushr 24)
            sr += ((add shr 16) and 0xFF) - ((rem shr 16) and 0xFF)
            sg += ((add shr 8) and 0xFF) - ((rem shr 8) and 0xFF)
            sb += (add and 0xFF) - (rem and 0xFF)
        }
    }
    return out
}

object ArtLoader {
    private val cache = LruCache<String, Bitmap>(48)
    fun get(key: String): Bitmap? = cache.get(key)
    fun put(key: String, bmp: Bitmap) { cache.put(key, bmp) }

    // Faixas sem capa: evita reabrir o arquivo toda vez que o item volta à tela
    private val noArt = java.util.concurrent.ConcurrentHashMap.newKeySet<String>()
    fun markNoArt(key: String) { noArt.add(key) }
    fun isNoArt(key: String): Boolean = noArt.contains(key)
    fun clear() { cache.evictAll(); noArt.clear() }

    // No máximo 2 capas decodificando ao mesmo tempo: evita disputar CPU/disco com a UI.
    private val gate = Semaphore(2)
    suspend fun loadGated(context: Context, track: Track): Bitmap? =
        if (isNoArt(track.path)) null
        else gate.withPermit { withContext(Dispatchers.IO) { load(context, track) } }

    fun load(context: Context, track: Track): Bitmap? {
        cache.get(track.path)?.let { return it }
        if (track.isSlac) {
            val bmp = runCatching {
                context.contentResolver.openInputStream(Uri.parse(track.path))?.use {
                    SlacHeaderParser.parse(it)
                }?.coverBytes
            }.getOrNull()
                ?.let { cb -> decodeThumb(cb) }
            if (bmp != null) cache.put(track.path, bmp) else markNoArt(track.path)
            return bmp
        }
        val bmp = runCatching {
            val r = MediaMetadataRetriever()
            r.setDataSource(context, Uri.parse(track.path))
            val bytes = r.embeddedPicture
            r.release()
            bytes?.let { decodeThumb(it) }
        }.getOrNull()
        if (bmp != null) cache.put(track.path, bmp) else markNoArt(track.path)
        return bmp
    }
}