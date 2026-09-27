package com.slacodec.app.playback

import android.content.Context
import android.net.Uri
import com.slacodec.app.codec.SlacodecJni
import java.io.File
import java.io.FileOutputStream

class SlacPlayer(private val context: Context) : Player {
    private val jni = SlacodecJni()
    private var handle: Long = 0L
    private var cachedPath: String? = null
    private var started = false
    private var stopped = false
    private var baseMs = 0L
    private var playStart = 0L
    override var onPlaybackStateChanged: ((Boolean) -> Unit)? = null

    fun loadFile(uriString: String): Boolean {
        releaseNative()
        handle = jni.nativeCreate()
        cachedPath = copyUriToCache(Uri.parse(uriString))
        val ok = jni.nativeOpen(handle, cachedPath!!)
        started = false
        stopped = false
        baseMs = 0L
        return ok
    }

    override fun play() {
        if (handle == 0L || cachedPath == null) return
        if (stopped) {
            jni.nativeRelease(handle)
            handle = jni.nativeCreate()
            jni.nativeOpen(handle, cachedPath!!)
            stopped = false
            baseMs = 0L
        }
        if (!started) {
            started = jni.nativePlay(handle)
            playStart = System.currentTimeMillis()
            onPlaybackStateChanged?.invoke(started)
        }
    }

    override fun pause() {
        if (started) {
            baseMs = positionMs()
            jni.nativeStop(handle)
            started = false
            stopped = true
            onPlaybackStateChanged?.invoke(false)
        }
    }

    override fun positionMs(): Long =
        if (started) baseMs + (System.currentTimeMillis() - playStart) else baseMs

    override fun durationMs(): Long =
        if (handle != 0L) jni.nativeGetDurationMs(handle) else 0L

    override fun seekTo(ms: Long) { /* TODO: nativeSeek via seek table */ }

    override fun release() = releaseNative()

    fun setSpatialParams(wideness: Float, reverbWet: Float) {
        if (handle != 0L) {
            jni.nativeSetWideness(handle, wideness)
            jni.nativeSetReverbWet(handle, reverbWet)
        }
    }

    private fun releaseNative() {
        if (handle != 0L) {
            jni.nativeStop(handle)
            jni.nativeRelease(handle)
            handle = 0L
        }
        started = false
        stopped = false
        baseMs = 0L
    }

    private fun copyUriToCache(uri: Uri): String {
        val f = File(context.cacheDir, "current.slac")
        if (f.exists()) f.delete()
        if (uri.scheme == "content") {
            context.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(f).use { out -> input.copyTo(out) }
            } ?: throw IllegalStateException("Nao foi possivel abrir a URI")
        } else {
            val src = File(uri.path ?: uri.toString())
            src.inputStream().use { input ->
                FileOutputStream(f).use { out -> input.copyTo(out) }
            }
        }
        return f.absolutePath
    }
}
