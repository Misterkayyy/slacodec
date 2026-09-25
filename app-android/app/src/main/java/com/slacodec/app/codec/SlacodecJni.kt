package com.slacodec.app.codec

class SlacodecJni {
    companion object {
        init {
            System.loadLibrary("slacodec_jni")
        }
    }

    private var nativeContext: Long = 0L

    init {
        nativeContext = nativeInitPlayer()
    }

    fun loadFile(filePath: String): Boolean {
        return nativeLoadFile(nativeContext, filePath)
    }

    fun play(): Boolean {
        return nativePlay(nativeContext)
    }

    fun pause(): Boolean {
        return nativePause(nativeContext)
    }

    fun setSpatialParams(wideness: Float, reverbWet: Float) {
        nativeSetSpatialParams(nativeContext, wideness, reverbWet)
    }

    fun release() {
        if (nativeContext != 0L) {
            nativeRelease(nativeContext)
            nativeContext = 0L
        }
    }

    // Garante que a memória nativa seja liberada se o GC do Kotlin coletar o objeto
    protected fun finalize() {
        release()
    }

    private external fun nativeInitPlayer(): Long
    private external fun nativeLoadFile(context: Long, filePath: String): Boolean
    private external fun nativePlay(context: Long): Boolean
    private external fun nativePause(context: Long): Boolean
    private external fun nativeSetSpatialParams(context: Long, wideness: Float, reverbWet: Float)
    private external fun nativeRelease(context: Long)
}
