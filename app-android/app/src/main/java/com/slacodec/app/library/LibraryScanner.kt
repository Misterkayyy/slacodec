package com.slacodec.app.library

import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.MediaStore
import androidx.documentfile.provider.DocumentFile
import com.slacodec.app.model.Track

object LibraryScanner {
    private val AUDIO_EXTENSIONS = setOf("mp3", "flac", "wav", "ogg", "opus", "m4a", "aac")

    fun scan(context: Context): List<Track> {
        val result = mutableListOf<Track>()
        result += scanMediaStore(context)
        for (tree in savedTrees(context)) {
            result += scanTree(context, Uri.parse(tree))
        }
        return result.sortedBy { it.title.lowercase() }
    }

    fun savedTrees(context: Context): Set<String> =
        context.getSharedPreferences("slac_prefs", Context.MODE_PRIVATE)
            .getStringSet("trees", emptySet()) ?: emptySet()

    fun addTree(context: Context, uri: Uri) {
        runCatching {
            context.contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        }
        val prefs = context.getSharedPreferences("slac_prefs", Context.MODE_PRIVATE)
        val set = savedTrees(context).toMutableSet()
        set.add(uri.toString())
        prefs.edit().putStringSet("trees", set).apply()
    }

    fun removeTree(context: Context, uriString: String) {
        runCatching {
            context.contentResolver.releasePersistableUriPermission(
                Uri.parse(uriString), Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        }
        val prefs = context.getSharedPreferences("slac_prefs", Context.MODE_PRIVATE)
        val set = savedTrees(context).toMutableSet()
        set.remove(uriString)
        prefs.edit().putStringSet("trees", set).apply()
    }

    private fun scanMediaStore(context: Context): List<Track> {
        val out = mutableListOf<Track>()
        val projection = arrayOf(
            MediaStore.Audio.Media._ID,
            MediaStore.Audio.Media.DISPLAY_NAME,
            MediaStore.Audio.Media.TITLE,
            MediaStore.Audio.Media.ALBUM,
            MediaStore.Audio.Media.DURATION,
            MediaStore.Audio.Media.SIZE,
            MediaStore.Audio.Media.BITRATE
        )
        runCatching {
            context.contentResolver.query(
                MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
                projection, null, null, MediaStore.Audio.Media.TITLE
            )?.use { c ->
                val iId = c.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
                val iName = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DISPLAY_NAME)
                val iTitle = c.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
                val iAlbum = c.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM)
                val iDur = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
                val iSize = c.getColumnIndexOrThrow(MediaStore.Audio.Media.SIZE)
                val iBit = c.getColumnIndexOrThrow(MediaStore.Audio.Media.BITRATE)
                while (c.moveToNext()) {
                    val name = c.getString(iName) ?: continue
                    val ext = name.substringAfterLast('.', "").lowercase()
                    if (ext !in AUDIO_EXTENSIONS) continue
                    val id = c.getLong(iId)
                    val uri = ContentUris.withAppendedId(
                        MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, id
                    )
                    val duration = c.getLong(iDur)
                    val size = c.getLong(iSize)
                    var bitrate = c.getLong(iBit) / 1000
                    if (bitrate <= 0 && duration > 0) bitrate = size * 8 / duration
                    out += Track(
                        path = uri.toString(),
                        title = c.getString(iTitle) ?: name.substringBeforeLast('.'),
                        album = c.getString(iAlbum) ?: "Album desconhecido",
                        format = ext.uppercase(),
                        sizeBytes = size,
                        bitrateKbps = bitrate.toInt(),
                        durationMs = duration
                    )
                }
            }
        }
        return out
    }

    private fun scanTree(context: Context, treeUri: Uri): List<Track> {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: return emptyList()
        val out = mutableListOf<Track>()
        fun walk(dir: DocumentFile, depth: Int) {
            if (depth > 6) return
            for (doc in dir.listFiles()) {
                if (doc.isDirectory) { walk(doc, depth + 1); continue }
                val name = doc.name ?: continue
                if (!name.endsWith(".slac", ignoreCase = true)) continue
                runCatching {
                    val info = context.contentResolver.openInputStream(doc.uri)?.use {
                        SlacHeaderParser.parse(it)
                    }
                    val size = doc.length()
                    val duration = if (info != null && info.sampleRate > 0)
                        info.totalSamples * 1000L / info.sampleRate else 0L
                    val bitrate = if (duration > 0) (size * 8 / duration).toInt() else 0
                    val tags = info?.tags ?: emptyMap()
                    out += Track(
                        path = doc.uri.toString(),
                        title = tags["TITLE"] ?: name.substringBeforeLast('.'),
                        artist = tags["ARTIST"] ?: "",
                        year = tags["DATE"] ?: "",
                        album = tags["ALBUM"] ?: "SLAC Lossless",
                        format = "SLAC",
                        sizeBytes = size,
                        bitrateKbps = bitrate,
                        durationMs = duration,
                        sampleRateHz = info?.sampleRate ?: 0,
                        bitsPerSample = info?.bits ?: 0,
                        channels = info?.channels ?: 0
                    )
                }
            }
        }
        walk(root, 0)
        return out
    }
}
