package com.slacodec.app.playback

import android.content.Context
import com.slacodec.app.codec.SlacodecJni

class SlacPlayer(private val context: Context) : Player {
    private val jni = SlacodecJni()
    override var onPlaybackStateChanged: ((Boolean) -> Unit)? = null
    
    fun loadFile(uriString: String): Boolean {
        return jni.testLoad(uriString) // Chama o C++
    }
    
    override fun play() { onPlaybackStateChanged?.invoke(true) }
    override fun pause() { onPlaybackStateChanged?.invoke(false) }
    override fun release() { /* Cleanup JNI */ }
    
    fun setSpatialParams(wideness: Float, reverbWet: Float) {
        // TODO: Conectar ao C++ real
    }
}
