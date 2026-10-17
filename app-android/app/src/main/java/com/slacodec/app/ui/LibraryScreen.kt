package com.slacodec.app.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.library.TagLoader
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatDuration
import com.slacodec.app.model.formatSize
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.withContext
import kotlin.math.abs

@Composable
fun LibraryScreen(
    tracks: List<Track>,
    loading: Boolean = false,
    onTracksLoaded: (List<Track>) -> Unit = {},
    currentPath: String?,
    isPlaying: Boolean,
    onTrackClick: (Track) -> Unit,
    onAddFolder: () -> Unit,
    onSettings: () -> Unit,
    bottomBar: @Composable () -> Unit = {}
) {
    val cs = MaterialTheme.colorScheme
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
             it.artist.contains(query, ignoreCase = true) ||
             it.album.contains(query, ignoreCase = true))
        }
    }

    val context = LocalContext.current
    val listState = rememberLazyListState()
    val filteredState = rememberUpdatedState(filtered)
    val tracksState = rememberUpdatedState(tracks)
    val loadedCallback = rememberUpdatedState(onTracksLoaded)

    // Leitura em blocos de 30: nada é lido enquanto a lista está rolando. Quando a rolagem assenta
    // (250 ms), lê o bloco de 30 itens ao redor da parte visível, do centro para fora, e entrega
    // o resultado de 10 em 10. Voltar a rolar cancela a leitura em andamento.
    LaunchedEffect(listState, query, filter) {
        snapshotFlow {
            if (listState.isScrollInProgress) null
            else {
                val v = listState.layoutInfo.visibleItemsInfo
                if (v.isEmpty()) null else v.first().index to v.last().index
            }
        }.collectLatest { w ->
            if (w == null) return@collectLatest
            delay(250)
            val list = filteredState.value
            if (list.isEmpty()) return@collectLatest
            val from = (w.first - 5).coerceAtLeast(0)
            val to = maxOf(from + 29, w.second).coerceAtMost(list.lastIndex)
            val center = (w.first + w.second) / 2
            val todo = (from..to).filter { !list[it].tagsLoaded }
                .sortedBy { abs(it - center) }
                .map { list[it] }
            for (chunk in todo.chunked(10)) {
                val res = coroutineScope {
                    chunk.map { t -> async { TagLoader.load(context, t) } }.awaitAll()
                }
                loadedCallback.value(res)
            }
        }
    }

    // Só quando o usuário está buscando: completa as tags restantes, 30 por vez,
    // para a busca por artista/álbum achar tudo. Sem busca, nada além do bloco visível é lido.
    val searching = query.isNotBlank()
    LaunchedEffect(searching) {
        if (!searching) return@LaunchedEffect
        delay(300)
        while (true) {
            val batch = tracksState.value.asSequence().filter { !it.tagsLoaded }.take(30).toList()
            if (batch.isEmpty()) break
            for (chunk in batch.chunked(10)) {
                val res = coroutineScope {
                    chunk.map { t -> async { TagLoader.load(context, t) } }.awaitAll()
                }
                loadedCallback.value(res)
            }
        }
    }

    // O padding das barras do sistema fica aqui, uma única vez, para lista e mini player.
    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        Row(
            Modifier
                .fillMaxWidth()
                .statusBarsPadding()
                .padding(start = 20.dp, end = 8.dp, top = 12.dp, bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text("Biblioteca", style = MaterialTheme.typography.headlineMedium)
                Text(
                    if (tracks.size == 1) "1 faixa" else "${tracks.size} faixas",
                    style = MaterialTheme.typography.bodySmall,
                    color = cs.onSurfaceVariant
                )
            }
            IconTap(IconPaths.GEAR, cs.onSurface, 22.dp, 10.dp) { onSettings() }
        }

        SearchField(query) { query = it }

        LazyRow(
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(formats) { f -> PillChip(f, filter == f) { filter = f } }
        }

        Box(Modifier.weight(1f).fillMaxWidth()) {
            when {
                tracks.isEmpty() && loading -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = cs.primary, strokeWidth = 3.dp)
                }
                tracks.isEmpty() -> EmptyState(onAddFolder)
                filtered.isEmpty() -> Box(
                    Modifier.fillMaxSize().padding(32.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text("Nada encontrado para a busca.", color = cs.onSurfaceVariant)
                }
                else -> LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    state = listState,
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp)
                ) {
                    items(filtered, key = { it.path }, contentType = { "track" }) { t ->
                        val current = t.path == currentPath
                        TrackRow(
                            track = t,
                            current = current,
                            playing = current && isPlaying,
                            onClick = { onTrackClick(t) }
                        )
                    }
                }
            }
            if (loading && tracks.isNotEmpty()) {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth().height(2.dp).align(Alignment.TopCenter),
                    color = cs.primary,
                    trackColor = androidx.compose.ui.graphics.Color.Transparent
                )
            }
        }
        bottomBar()
    }
}

@Composable
private fun SearchField(query: String, onChange: (String) -> Unit) {
    val cs = MaterialTheme.colorScheme
    Row(
        Modifier
            .padding(horizontal = 20.dp)
            .fillMaxWidth()
            .height(46.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(cs.surfaceVariant)
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        PathIcon(IconPaths.SEARCH, cs.onSurfaceVariant, 20.dp)
        Spacer(Modifier.width(10.dp))
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (query.isEmpty()) {
                Text("Buscar músicas, artistas, álbuns", color = cs.onSurfaceVariant, fontSize = 15.sp)
            }
            BasicTextField(
                value = query,
                onValueChange = onChange,
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyLarge.copy(color = cs.onSurface),
                cursorBrush = SolidColor(cs.primary),
                modifier = Modifier.fillMaxWidth()
            )
        }
        if (query.isNotEmpty()) {
            IconTap(IconPaths.CLOSE, cs.onSurfaceVariant, 16.dp, 6.dp) { onChange("") }
        }
    }
}

@Composable
fun TrackRow(track: Track, current: Boolean, playing: Boolean, onClick: () -> Unit) {
    val cs = MaterialTheme.colorScheme
    val subtitle = listOf(track.artist, track.album).filter { it.isNotEmpty() }
        .joinToString("  •  ").ifEmpty { formatSize(track.sizeBytes) }
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(if (current) cs.primary.copy(alpha = 0.10f) else androidx.compose.ui.graphics.Color.Transparent)
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
                color = if (current) cs.primary else cs.onSurface
            )
            Text(
                subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = cs.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        Spacer(Modifier.width(10.dp))
        Column(horizontalAlignment = Alignment.End) {
            if (playing) EqBars()
            else Text(
                if (track.tagsLoaded) formatDuration(track.durationMs) else "–:––",
                style = MaterialTheme.typography.labelMedium,
                color = cs.onSurfaceVariant
            )
            Spacer(Modifier.height(3.dp))
            Text(
                track.format,
                style = MaterialTheme.typography.labelSmall,
                color = cs.onSurfaceVariant.copy(alpha = 0.7f),
                letterSpacing = 0.5.sp
            )
        }
    }
}

@Composable
fun ArtThumb(track: Track) {
    val context = LocalContext.current
    var bmp by remember(track.path, track.tagsLoaded) { mutableStateOf(ArtLoader.get(track.path)) }
    LaunchedEffect(track.path, track.tagsLoaded) {
        // a capa vem junto com a leitura das tags; só relê se foi descartada do cache
        if (track.tagsLoaded && bmp == null) {
            bmp = ArtLoader.loadGated(context, track)
        }
    }
    val b = bmp
    val shape = RoundedCornerShape(12.dp)
    if (b != null) {
        Image(
            bitmap = remember(b) { b.asImageBitmap() },
            contentDescription = null,
            modifier = Modifier.size(52.dp).clip(shape),
            contentScale = ContentScale.Crop,
            filterQuality = FilterQuality.Low
        )
    } else {
        GradientPlaceholder(Modifier.size(52.dp).clip(shape), if (track.isSlac) "S" else "♪")
    }
}

@Composable
fun EmptyState(onAddFolder: () -> Unit) {
    Column(
        Modifier.fillMaxSize().padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        GradientPlaceholder(Modifier.size(112.dp).clip(CircleShape), "♪", 44.sp)
        Spacer(Modifier.height(24.dp))
        Text("Escolha sua pasta de músicas", style = MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(6.dp))
        Text(
            "O app lista apenas os arquivos da pasta que você selecionar\n(SLAC, FLAC, MP3, WAV, OGG, M4A e outros).",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center
        )
        Spacer(Modifier.height(20.dp))
        Button(onClick = onAddFolder) { Text("Escolher pasta") }
    }
}