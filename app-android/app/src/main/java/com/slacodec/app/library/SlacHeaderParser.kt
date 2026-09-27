package com.slacodec.app.library

import java.io.BufferedInputStream
import java.io.InputStream

object SlacHeaderParser {
    data class Info(val sampleRate: Int, val bits: Int, val channels: Int, val totalSamples: Long)

    private const val CHUNK_FMT = 0x20746D66L // 'f','m','t',' '

    fun parse(input: InputStream): Info? {
        val d = BufferedInputStream(input)
        var guard = 0
        while (guard++ < 32) {
            val id = readU32(d) ?: return null
            val size = readU32(d) ?: return null
            if (readU32(d) == null) return null // crc
            if (id == CHUNK_FMT) {
                val sr = readU32(d)?.toInt() ?: return null
                val bits = d.read()
                val ch = d.read()
                d.read(); d.read() // mode + reserved
                val total = readU64(d) ?: return null
                return Info(sr, bits, ch, total)
            } else {
                var rem = size
                while (rem > 0) {
                    val skipped = d.skip(rem)
                    if (skipped <= 0) { if (d.read() == -1) return null; rem -= 1 }
                    else rem -= skipped
                }
            }
        }
        return null
    }

    private fun readU32(d: InputStream): Long? {
        val b0 = d.read(); val b1 = d.read(); val b2 = d.read(); val b3 = d.read()
        if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) return null
        return b0.toLong() or (b1.toLong() shl 8) or (b2.toLong() shl 16) or (b3.toLong() shl 24)
    }

    private fun readU64(d: InputStream): Long? {
        var v = 0L
        for (i in 0 until 8) {
            val b = d.read()
            if (b < 0) return null
            v = v or (b.toLong() shl (8 * i))
        }
        return v
    }
}
