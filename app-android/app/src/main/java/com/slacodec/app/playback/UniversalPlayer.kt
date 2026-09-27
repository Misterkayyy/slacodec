package com.slacodec.app.playback

import android.content.Context
import androidx.media3.common.MediaItem
import androidx.media3.exoplayer.ExoPlayer

class UniversalPlayer(private val context: Context) : Player {
    private val exoPlayer = ExoPlayer.Builder(context).build()
    override var onPlaybackStateChanged: ((Boolean) -> Unit)? = null
    
    fun loadFile(uriString: String): Boolean {
        return try {
            val mediaItem = MediaItem.fromUri(uriString)
            exoPlayer.setMediaItem(mediaItem)
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
    
    override fun release() { exoPlayer.release() }
}
