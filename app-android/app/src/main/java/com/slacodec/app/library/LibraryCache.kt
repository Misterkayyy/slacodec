package com.slacodec.app.library

import android.content.Context
import com.slacodec.app.model.Track
import org.json.JSONArray
import org.json.JSONObject

object LibraryCache {
    private const val KEY = "library_cache_v2"

    fun save(context: Context, tracks: List<Track>) {
        val arr = JSONArray()
        for (t in tracks) {
            arr.put(JSONObject().apply {
                put("path", t.path); put("title", t.title); put("album", t.album)
                put("format", t.format); put("size", t.sizeBytes)
                put("bitrate", t.bitrateKbps); put("duration", t.durationMs)
                put("sr", t.sampleRateHz); put("bits", t.bitsPerSample); put("ch", t.channels)
                put("artist", t.artist); put("year", t.year)
                put("genre", t.genre); put("track", t.trackNumber); put("comment", t.comment); put("mod", t.lastModified); put("loaded", t.tagsLoaded)
            })
        }
        context.getSharedPreferences("slac_prefs", Context.MODE_PRIVATE)
            .edit().putString(KEY, arr.toString()).apply()
    }

    fun load(context: Context): List<Track> {
        val json = context.getSharedPreferences("slac_prefs", Context.MODE_PRIVATE)
            .getString(KEY, null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(json)
            (0 until arr.length()).map { i ->
                val o = arr.getJSONObject(i)
                Track(
                    path = o.getString("path"),
                    title = o.getString("title"),
                    album = o.getString("album"),
                    format = o.getString("format"),
                    sizeBytes = o.getLong("size"),
                    bitrateKbps = o.getInt("bitrate"),
                    durationMs = o.getLong("duration"),
                    sampleRateHz = o.optInt("sr"),
                    bitsPerSample = o.optInt("bits"),
                    channels = o.optInt("ch"),
                    artist = o.optString("artist", ""),
                    year = o.optString("year", ""),
                    genre = o.optString("genre", ""),
                    trackNumber = o.optString("track", ""),
                    comment = o.optString("comment", ""),
                    lastModified = o.optLong("mod", 0L),
                    tagsLoaded = o.optBoolean("loaded", true)
                )
            }
        }.getOrDefault(emptyList())
    }
}