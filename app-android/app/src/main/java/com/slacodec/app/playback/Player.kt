package com.slacodec.app.playback

interface Player {
    fun play()
    fun pause()
    fun release()
    var onPlaybackStateChanged: ((isPlaying: Boolean) -> Unit)?
}
