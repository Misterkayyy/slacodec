package com.slacodec.app.playback

interface Player {
    fun play()
    fun pause()
    fun release()
    fun positionMs(): Long
    fun durationMs(): Long
    fun seekTo(ms: Long)
    var onPlaybackStateChanged: ((isPlaying: Boolean) -> Unit)?
}
