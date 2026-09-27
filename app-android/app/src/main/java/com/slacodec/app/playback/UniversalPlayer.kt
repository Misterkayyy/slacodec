package com.slacodec.app.playback

import android.content.Context
import android.net.Uri
import androidx.media3.common.MediaItem
import androidx.media3.exoplayer.ExoPlayer
import java.io.File

class UniversalPlayer(private val context: Context) : Player {
    private val exoPlayer = ExoPlayer.Builder(context).build()
    override var onPlaybackStateChanged: ((Boolean) -> Unit)? = null

    fun loadFile(uriString: String): Boolean {
        return try {
            val uri = if (uriString.startsWith("content://") || uriString.startsWith("file://"))
                Uri.parse(uriString) else Uri.fromFile(File(uriString))
            exoPlayer.setMediaItem(MediaItem.fromUri(uri))
            exoPlayer.prepare()
            true
        } catch (e: Exception) { false }
    }

    override fun play() {
        exoPlayer.playWhenReady = true
        onPlaybackStateChanged?.invoke(true)
    }

    override fun pause() {
        exoPlayer.playWhenReady = false
        onPlaybackStateChanged?.invoke(false)
    }

    override fun positionMs(): Long = exoPlayer.currentPosition
    override fun durationMs(): Long = exoPlayer.duration.coerceAtLeast(0L)
    override fun seekTo(ms: Long) { exoPlayer.seekTo(ms) }
    override fun release() { exoPlayer.release() }
}
