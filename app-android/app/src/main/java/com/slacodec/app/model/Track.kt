package com.slacodec.app.model

data class Track(
    val path: String,
    val title: String,
    val album: String,
    val format: String,
    val sizeBytes: Long,
    val bitrateKbps: Int,
    val durationMs: Long,
    val sampleRateHz: Int = 0,
    val bitsPerSample: Int = 0,
    val channels: Int = 0
) {
    val isSlac: Boolean get() = format == "SLAC"
}

fun formatSize(bytes: Long): String =
    if (bytes >= 1048576) String.format("%.1f MB", bytes / 1048576.0)
    else String.format("%.0f KB", bytes / 1024.0)

fun formatDuration(ms: Long): String {
    val totalSec = ms / 1000
    return String.format("%d:%02d", totalSec / 60, totalSec % 60)
}

fun formatSampleRate(sr: Int): String = when {
    sr <= 0 -> ""
    sr % 1000 == 0 -> "${sr / 1000} kHz"
    else -> String.format("%.1f kHz", sr / 1000.0)
}
