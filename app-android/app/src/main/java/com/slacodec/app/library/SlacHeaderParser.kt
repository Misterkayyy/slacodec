package com.slacodec.app.library

import java.io.BufferedInputStream
import java.io.InputStream

object SlacHeaderParser {
    data class Info(
        val sampleRate: Int,
        val bits: Int,
        val channels: Int,
        val totalSamples: Long,
        val tags: Map<String, String> = emptyMap(),
        val coverBytes: ByteArray? = null
    )

    private const val CHUNK_FMT = 0x20746D66L   // 'f','m','t',' '
    private const val CHUNK_META = 0x6174656DL  // 'm','e','t','a'
    private const val CHUNK_COVR = 0x72766F63L  // 'c','o','v','r'
    private const val CHUNK_DATA = 0x61746164L  // 'd','a','t','a'

    fun parse(input: InputStream): Info? {
        val d = BufferedInputStream(input)
        var fmt: Info? = null
        var tags: Map<String, String>? = null
        var cover: ByteArray? = null
        var coverSeen = false
        var guard = 0
        while (guard++ < 64) {
            val id = readU32(d) ?: break
            val size = readU32(d) ?: break
            if (readU32(d) == null) break // crc
            when (id) {
                CHUNK_FMT -> {
                    val sr = readU32(d)?.toInt() ?: break
                    val bits = d.read()
                    val ch = d.read()
                    d.read(); d.read()
                    val total = readU64(d) ?: break
                    fmt = Info(sr, bits, ch, total)
                    skipFully(d, size - 24)
                }
                CHUNK_META -> {
                    val payload = readBytes(d, size) ?: break
                    tags = parseMeta(payload)
                }
                CHUNK_COVR -> {
                    val payload = readBytes(d, size) ?: break
                    coverSeen = true
                    if (payload.size > 4) {
                        val ml = le32(payload, 0)
                        if (ml in 0..(payload.size - 4)) {
                            cover = payload.copyOfRange(4 + ml, payload.size)
                        }
                    }
                }
                CHUNK_DATA -> break // audio comeca aqui: para de ler
                else -> skipFully(d, size)
            }
            if (fmt != null && tags != null && coverSeen) break
        }
        return fmt?.copy(tags = tags ?: emptyMap(), coverBytes = cover)
    }

    private fun parseMeta(p: ByteArray): Map<String, String> {
        val out = LinkedHashMap<String, String>()
        var pos = 0
        while (pos + 4 <= p.size) {
            val kl = le32(p, pos); pos += 4
            if (kl < 0 || pos + kl > p.size) break
            val k = String(p, pos, kl.toInt()); pos += kl
            if (pos + 4 > p.size) break
            val vl = le32(p, pos); pos += 4
            if (vl < 0 || pos + vl > p.size) break
            val v = String(p, pos, vl.toInt()); pos += vl
            out[k] = v
        }
        return out
    }

    private fun le32(p: ByteArray, i: Int): Int =
        (p[i].toInt() and 0xFF) or ((p[i+1].toInt() and 0xFF) shl 8) or
        ((p[i+2].toInt() and 0xFF) shl 16) or ((p[i+3].toInt() and 0xFF) shl 24)

    private fun skipFully(d: InputStream, n: Long) {
        var rem = n
        while (rem > 0) {
            val s = d.skip(rem)
            if (s <= 0) { if (d.read() == -1) return; rem -= 1 } else rem -= s
        }
    }

    private fun readBytes(d: InputStream, n: Long): ByteArray? {
        if (n < 0 || n > 8_000_000) return null
        val out = ByteArray(n.toInt())
        var off = 0
        while (off < out.size) {
            val r = d.read(out, off, out.size - off)
            if (r == -1) return null
            off += r
        }
        return out
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
