package com.slacodec.app.codec

class SlacodecJni {
    external fun nativeCreate(): Long
    external fun nativeOpen(handle: Long, path: String): Boolean
    external fun nativePlay(handle: Long): Boolean
    external fun nativeStop(handle: Long)

    // Novos métodos para Seek e Posição
    external fun nativeSeek(handle: Long, positionMs: Long): Boolean
    external fun nativeGetPositionMs(handle: Long): Long

    external fun nativeGetDurationMs(handle: Long): Long
    external fun nativeSetWideness(handle: Long, w: Float)
    external fun nativeSetReverbWet(handle: Long, wet: Float)

    // Novo método para HRIR
    external fun nativeLoadHrir(handle: Long, path: String): Boolean

    external fun nativeHasEmbeddedHrir(handle: Long): Boolean
    external fun nativeGetEmbeddedHrirType(handle: Long): String

    external fun nativeSetUseAuto(handle: Long, on: Boolean)
    external fun nativeProbe(path: String): String
    external fun nativeRelease(handle: Long)

    companion object {
        init {
            System.loadLibrary("slacodec_jni")
        }
    }
}