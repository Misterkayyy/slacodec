package com.slacodec.app.library

import android.content.Context
import android.content.Intent
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.DocumentsContract
import com.slacodec.app.model.Track
import com.slacodec.app.ui.ArtLoader
import com.slacodec.app.ui.decodeThumb

/**
 * Lista apenas as músicas das pastas escolhidas pelo usuário (Storage Access Framework).
 * Não consulta mais o MediaStore, então nada fora da pasta selecionada aparece.
 */
object LibraryScanner {
    private val AUDIO_EXTENSIONS = setOf("mp3", "flac", "wav", "ogg", "opus", "m4a", "aac")
    private const val MAX_DEPTH = 8

    private class Entry(val uri: Uri, val name: String, val ext: String, val size: Long, val modified: Long)

    /** Ordenação estável (nome do arquivo), independente de as tags já terem sido lidas. */
    private fun fileKey(path: String): String =
        Uri.decode(path).substringAfterLast('/').lowercase()

    /**
     * Varredura rápida: só LISTA os arquivos. As tags são lidas sob demanda (TagLoader),
     * apenas para os itens visíveis ou próximos na Biblioteca. Quem não mudou vem do cache.
     */
    fun scan(context: Context, cached: List<Track> = emptyList()): List<Track> {
        val entries = savedTrees(context).flatMap { listTree(context, Uri.parse(it)) }
        val known = cached.associateBy { it.path }
        return entries.map { e ->
            val c = known[e.uri.toString()]
            if (c != null && c.sizeBytes == e.size && c.lastModified == e.modified) c else placeholder(e)
        }.sortedBy { fileKey(it.path) }
    }

    private fun placeholder(e: Entry) = Track(
        path = e.uri.toString(),
        title = e.name.substringBeforeLast('.'),
        album = "",
        format = e.ext.uppercase(),
        sizeBytes = e.size,
        bitrateKbps = 0,
        durationMs = 0L,
        lastModified = e.modified,
        tagsLoaded = false
    )

    /** Lê as tags (e já guarda a miniatura da capa) de uma faixa não carregada. */
    fun readMetadata(context: Context, t: Track): Track {
        val ext = t.format.lowercase()
        val e = Entry(Uri.parse(t.path), t.title + "." + ext, ext, t.sizeBytes, t.lastModified)
        return runCatching {
            if (ext == "slac") readSlac(context, e) else readAudio(context, e)
        }.getOrNull() ?: t.copy(tagsLoaded = true)
    }

    private fun cacheThumb(key: String, bytes: ByteArray?) {
        val bmp = bytes?.let { decodeThumb(it) }
        if (bmp != null) ArtLoader.put(key, bmp) else ArtLoader.markNoArt(key)
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

    private fun listTree(context: Context, treeUri: Uri): List<Entry> {
        val out = mutableListOf<Entry>()
        val resolver = context.contentResolver
        val cols = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED
        )

        fun walk(parentId: String, depth: Int) {
            if (depth > MAX_DEPTH) return
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentId)
            val dirs = mutableListOf<String>()
            // Uma única consulta por pasta (bem mais rápido que DocumentFile.listFiles)
            runCatching {
                resolver.query(childrenUri, cols, null, null, null)?.use { c ->
                    while (c.moveToNext()) {
                        val id = c.getString(0) ?: continue
                        val name = c.getString(1) ?: continue
                        if (c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) { dirs += id; continue }
                        val ext = name.substringAfterLast('.', "").lowercase()
                        if (ext != "slac" && ext !in AUDIO_EXTENSIONS) continue
                        out += Entry(
                            uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, id),
                            name = name,
                            ext = ext,
                            size = if (c.isNull(3)) 0L else c.getLong(3),
                            modified = if (c.isNull(4)) 0L else c.getLong(4)
                        )
                    }
                }
            }
            dirs.forEach { walk(it, depth + 1) }
        }

        runCatching { walk(DocumentsContract.getTreeDocumentId(treeUri), 0) }
        return out
    }

    private fun readSlac(context: Context, e: Entry): Track {
        val uri = e.uri; val name = e.name; val size = e.size
        val info = context.contentResolver.openInputStream(uri)?.use { SlacHeaderParser.parse(it) }
        cacheThumb(uri.toString(), info?.coverBytes)
        val duration = if (info != null && info.sampleRate > 0)
            info.totalSamples * 1000L / info.sampleRate else 0L
        val bitrate = if (duration > 0) (size * 8 / duration).toInt() else 0
        val tags = info?.tags ?: emptyMap()
        return Track(
            path = uri.toString(),
            title = tags["TITLE"] ?: name.substringBeforeLast('.'),
            artist = tags["ARTIST"] ?: "",
            year = tags["DATE"] ?: tags["YEAR"] ?: "",
            genre = tags["GENRE"] ?: "",
            trackNumber = tags["TRACK"] ?: tags["TRACKNUMBER"] ?: "",
            comment = tags["COMMENT"] ?: "",
            album = tags["ALBUM"] ?: "SLAC Lossless",
            format = "SLAC",
            sizeBytes = size,
            bitrateKbps = bitrate,
            durationMs = duration,
            sampleRateHz = info?.sampleRate ?: 0,
            bitsPerSample = info?.bits ?: 0,
            channels = info?.channels ?: 0,
            lastModified = e.modified
        )
    }

    private fun readAudio(context: Context, e: Entry): Track {
        val uri = e.uri; val name = e.name; val ext = e.ext; val size = e.size
        val r = MediaMetadataRetriever()
        try {
            r.setDataSource(context, uri)
            fun meta(key: Int): String = r.extractMetadata(key)?.trim().orEmpty()
            val duration = meta(MediaMetadataRetriever.METADATA_KEY_DURATION).toLongOrNull() ?: 0L
            var bitrate = (meta(MediaMetadataRetriever.METADATA_KEY_BITRATE).toLongOrNull() ?: 0L) / 1000
            if (bitrate <= 0 && duration > 0) bitrate = size * 8 / duration
            cacheThumb(uri.toString(), r.embeddedPicture)
            return Track(
                path = uri.toString(),
                title = meta(MediaMetadataRetriever.METADATA_KEY_TITLE).ifEmpty { name.substringBeforeLast('.') },
                artist = meta(MediaMetadataRetriever.METADATA_KEY_ARTIST)
                    .ifEmpty { meta(MediaMetadataRetriever.METADATA_KEY_ALBUMARTIST) },
                album = meta(MediaMetadataRetriever.METADATA_KEY_ALBUM),
                year = meta(MediaMetadataRetriever.METADATA_KEY_YEAR),
                genre = meta(MediaMetadataRetriever.METADATA_KEY_GENRE),
                trackNumber = meta(MediaMetadataRetriever.METADATA_KEY_CD_TRACK_NUMBER),
                format = ext.uppercase(),
                sizeBytes = size,
                bitrateKbps = bitrate.toInt(),
                durationMs = duration,
                lastModified = e.modified
            )
        } finally {
            runCatching { r.release() }
        }
    }
}