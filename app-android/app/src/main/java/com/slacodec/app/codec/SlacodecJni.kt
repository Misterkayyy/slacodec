package com.slacodec.app.codec

class SlacodecJni {
    companion object {
        init { System.loadLibrary("slacodec_jni") }
    }

    external fun nativeCreate(): Long
    external fun nativeOpen(handle: Long, path: String): Boolean
    external fun nativePlay(handle: Long): Boolean
    external fun nativeStop(handle: Long)
    external fun nativeSetWideness(handle: Long, wideness: Float)
    external fun nativeSetReverbWet(handle: Long, wet: Float)
    external fun nativeGetDurationMs(handle: Long): Long
    external fun nativeRelease(handle: Long)
}
