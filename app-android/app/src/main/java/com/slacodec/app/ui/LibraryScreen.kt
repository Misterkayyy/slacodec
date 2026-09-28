package com.slacodec.app.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatDuration
import com.slacodec.app.model.formatSize
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LibraryScreen(
    tracks: List<Track>,
    currentPath: String?,
    isPlaying: Boolean,
    onTrackClick: (Track) -> Unit,
    onAddFolder: () -> Unit,
    onSettings: () -> Unit,
    bottomBar: @Composable () -> Unit = {}
) {
    var query by remember { mutableStateOf("") }
    val formats = remember(tracks) {
        listOf("Tudo") + tracks.map { it.format }.distinct().sorted()
    }
    var filter by remember { mutableStateOf("Tudo") }
    val filtered = remember(tracks, query, filter) {
        tracks.filter {
            (filter == "Tudo" || it.format == filter) &&
            (query.isBlank() ||
             it.title.contains(query, ignoreCase = true) ||
             it.album.contains(query, ignoreCase = true))
        }
    }

    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(start = 20.dp, end = 12.dp, top = 16.dp, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    "SLACODEC",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 3.sp
                )
                Text("Biblioteca", style = MaterialTheme.typography.headlineMedium)
                Text(
                    "${tracks.size} faixas",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Text(
                "⚙\uFE0E",
                fontSize = 22.sp,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier
                    .clip(RoundedCornerShape(12.dp))
                    .clickable { onSettings() }
                    .padding(10.dp)
            )
        }

        OutlinedTextField(
            value = query,
            onValueChange = { query = it },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp),
            placeholder = { Text("Buscar musicas...") },
            singleLine = true,
            shape = RoundedCornerShape(14.dp),
            trailingIcon = {
                if (query.isNotEmpty()) {
                    Text(
                        "✕",
                        modifier = Modifier.padding(end = 14.dp).clickable { query = "" },
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        )

        LazyRow(
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(formats) { f ->
                FilterChip(
                    selected = filter == f,
                    onClick = { filter = f },
                    label = { Text(f, fontSize = 12.sp) }
                )
            }
        }

        when {
            tracks.isEmpty() -> EmptyState(onAddFolder)
            filtered.isEmpty() -> Box(
                Modifier.fillMaxSize().padding(32.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    "Nada encontrado para a busca.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            else -> LazyColumn(
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp)
            ) {
                items(filtered, key = { it.path }) { t ->
                    TrackRow(
                        track = t,
                        playing = t.path == currentPath && isPlaying,
                        onClick = { onTrackClick(t) }
                    )
                }
            }
        }
        bottomBar()
    }
}

@Composable
fun TrackRow(track: Track, playing: Boolean, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        ArtThumb(track)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(
                track.title,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                color = if (playing) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.onSurface
            )
            Text(
                track.subtitle(),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        Spacer(Modifier.width(10.dp))
        Column(horizontalAlignment = Alignment.End) {
            if (playing) EqBars()
            else Text(
                formatDuration(track.durationMs),
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(3.dp))
            FormatBadge(track.format, track.isSlac)
        }
    }
}

@Composable
fun ArtThumb(track: Track) {
    val context = LocalContext.current
    var bmp by remember(track.path) { mutableStateOf(ArtLoader.get(track.path)) }
    LaunchedEffect(track.path) {
        if (bmp == null) {
            bmp = withContext(Dispatchers.IO) { ArtLoader.load(context, track) }
        }
    }
    val b = bmp
    if (b != null) {
        Image(
            bitmap = b.asImageBitmap(),
            contentDescription = null,
            modifier = Modifier.size(48.dp).clip(RoundedCornerShape(10.dp)),
            contentScale = ContentScale.Crop,
            filterQuality = FilterQuality.High
        )
    } else {
        GradientPlaceholder(
            Modifier.size(48.dp).clip(RoundedCornerShape(10.dp)),
            if (track.isSlac) "S" else "♪"
        )
    }
}

@Composable
fun EmptyState(onAddFolder: () -> Unit) {
    Column(
        Modifier.fillMaxSize().padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        GradientPlaceholder(Modifier.size(120.dp).clip(CircleShape), "♪", 48.sp)
        Spacer(Modifier.height(20.dp))
        Text("Sua biblioteca esta vazia", style = MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(6.dp))
        Text(
            "MP3, FLAC, WAV e OGG aparecem automaticamente.\nPara arquivos .slac, adicione uma pasta.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = androidx.compose.ui.text.style.TextAlign.Center
        )
        Spacer(Modifier.height(16.dp))
        TextButton(onClick = onAddFolder) { Text("Adicionar pasta .slac") }
    }
}
