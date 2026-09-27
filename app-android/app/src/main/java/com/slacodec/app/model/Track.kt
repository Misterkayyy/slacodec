package com.slacodec.app.model

data class Track(
    val path: String,
    val title: String,
    val album: String,
    val format: String,
    val sizeBytes: Long,
    val bitrateKbps: Int,
    val durationMs: Long
) {
    val isSlac: Boolean get() = format == "SLAC"
}

fun formatSize(bytes: Long): String =
    if (bytes >= 1048576) String.format("%.1f MB", bytes / 1048576.0)
    else String.format("%.0f KB", bytes / 1024.0)

fun formatDuration(ms: Long): String {
    val totalSec = ms / 1000
    val min = totalSec / 60
    val sec = totalSec % 60
    return String.format("%d:%02d", min, sec)
}
