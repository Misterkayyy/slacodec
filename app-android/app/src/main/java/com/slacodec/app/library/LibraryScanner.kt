package com.slacodec.app.library

import android.media.MediaMetadataRetriever
import android.os.Environment
import com.slacodec.app.codec.SlacodecJni
import com.slacodec.app.model.Track
import java.io.File

object LibraryScanner {
    private val EXTENSIONS = setOf("slac", "mp3", "flac", "wav", "ogg", "opus", "m4a", "aac")
    private val jni = SlacodecJni()

    fun scan(): List<Track> {
        val root = Environment.getExternalStorageDirectory()
        return root.walkTopDown(maxDepth = 6)
            .filter { it.isFile && it.extension.lowercase() in EXTENSIONS }
            .mapNotNull { runCatching { buildTrack(it) }.getOrNull() }
            .sortedBy { it.title.lowercase() }
            .toList()
    }

    private fun buildTrack(f: File): Track {
        val ext = f.extension.lowercase()
        if (ext == "slac") {
            // Metadata lossless direto do container SLAC via JNI
            val parts = jni.nativeProbe(f.absolutePath).split("|")
            val totalSamples = parts.getOrNull(0)?.toLongOrNull() ?: 0L
            val sr = parts.getOrNull(1)?.toLongOrNull()?.takeIf { it > 0 } ?: 44100L
            val duration = totalSamples * 1000L / sr
            val bitrate = if (duration > 0) (f.length() * 8L / duration).toInt() else 0
            return Track(
                path = f.absolutePath,
                title = f.nameWithoutExtension,
                album = f.parentFile?.name ?: "SLAC",
                format = "SLAC",
                sizeBytes = f.length(),
                bitrateKbps = bitrate,
                durationMs = duration
            )
        }

        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(f.absolutePath)
            val duration = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            val bitrate = (r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITRATE)?.toLongOrNull() ?: 0L) / 1000
            Track(
                path = f.absolutePath,
                title = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE) ?: f.nameWithoutExtension,
                album = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM) ?: "Album desconhecido",
                format = ext.uppercase(),
                sizeBytes = f.length(),
                bitrateKbps = bitrate.toInt(),
                durationMs = duration
            )
        } finally {
            r.release()
        }
    }
}
