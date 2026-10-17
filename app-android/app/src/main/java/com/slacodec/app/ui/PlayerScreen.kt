package com.slacodec.app.ui

import android.graphics.Bitmap
import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatDuration
import com.slacodec.app.model.formatSampleRate
import com.slacodec.app.model.formatSize
import kotlin.math.abs

private data class SpatialPreset(val name: String, val wideness: Float, val wet: Float)

private val PRESETS = listOf(
    SpatialPreset("Direto", 1.0f, 0.0f),
    SpatialPreset("Fones", 1.2f, 0.15f),
    SpatialPreset("Sala", 1.4f, 0.30f),
    SpatialPreset("Salão", 1.7f, 0.45f),
    SpatialPreset("Palco", 2.0f, 0.25f)
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PlayerScreen(
    track: Track,
    art: Bitmap?,
    artBlur: Bitmap?,
    isPlaying: Boolean,
    positionMs: Long,
    wideness: Float,
    reverbWet: Float,
    extraInfo: String?,
    tags: Map<String, String> = emptyMap(),
    isFavorite: Boolean,
    shuffle: Boolean,
    repeatMode: Int,
    queue: List<Track>,
    queueIndex: Int,
    onSelectQueue: (Int) -> Unit,
    onBack: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrev: () -> Unit,
    onSeek: (Long) -> Unit,
    onSpatialChange: (Float, Float) -> Unit,
    onToggleFavorite: () -> Unit,
    onToggleShuffle: () -> Unit,
    onToggleRepeat: () -> Unit
) {
    val cs = MaterialTheme.colorScheme
    val duration = track.durationMs.coerceAtLeast(1L)
    var showSpatial by remember { mutableStateOf(false) }
    var showQueue by remember { mutableStateOf(false) }
    var showInfo by remember { mutableStateOf(false) }
    val isDark = cs.background.red < 0.5f
    var seekPosition by remember(positionMs) { mutableStateOf(positionMs.toFloat()) }
    var isDragging by remember { mutableStateOf(false) }
    val coverScale by animateFloatAsState(
        targetValue = if (isPlaying) 1f else 0.88f,
        animationSpec = spring(dampingRatio = 0.7f, stiffness = 220f),
        label = "cover"
    )
    val info = extraInfo ?: listOfNotNull(
        formatSampleRate(track.sampleRateHz).ifEmpty { null },
        if (track.bitsPerSample > 0) "${track.bitsPerSample}-bit" else null
    ).joinToString("  •  ").ifEmpty { null }

    Box(Modifier.fillMaxSize().background(cs.background)) {
        Crossfade(targetState = artBlur, animationSpec = tween(500), label = "bg") { blur ->
            if (blur != null) {
                Image(
                    bitmap = remember(blur) { blur.asImageBitmap() },
                    contentDescription = null,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Crop,
                    filterQuality = FilterQuality.High
                )
            }
        }
        Box(
            Modifier.fillMaxSize().background(
                Brush.verticalGradient(
                    listOf(
                        cs.background.copy(alpha = if (isDark) 0.55f else 0.35f),
                        cs.background.copy(alpha = if (isDark) 0.92f else 0.85f)
                    )
                )
            )
        )

        Column(
            Modifier
                .fillMaxSize()
                .statusBarsPadding()
                .navigationBarsPadding()
                .padding(horizontal = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.rotate(-90f)) { IconTap(IconPaths.BACK, cs.onBackground, 22.dp) { onBack() } }
                Spacer(Modifier.weight(1f))
                Text(
                    "TOCANDO AGORA",
                    style = MaterialTheme.typography.labelSmall,
                    color = cs.onSurfaceVariant,
                    letterSpacing = 2.sp
                )
                Spacer(Modifier.weight(1f))
                IconTap(IconPaths.QUEUE, cs.onBackground, 22.dp) { showQueue = true }
            }

            // Capa: ocupa o espaço livre e se ajusta a telas baixas
            Box(Modifier.weight(1f).fillMaxWidth().padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                Box(
                    Modifier
                        .aspectRatio(1f)
                        .graphicsLayer { scaleX = coverScale; scaleY = coverScale }
                        .shadow(28.dp, RoundedCornerShape(26.dp))
                        .clip(RoundedCornerShape(26.dp))
                        .background(cs.surfaceVariant)
                ) {
                    Crossfade(targetState = art, animationSpec = tween(350), label = "cover-art") { a ->
                        if (a != null) {
                            Image(
                                bitmap = remember(a) { a.asImageBitmap() },
                                contentDescription = null,
                                modifier = Modifier.fillMaxSize(),
                                contentScale = ContentScale.Crop,
                                filterQuality = FilterQuality.High
                            )
                        } else {
                            GradientPlaceholder(Modifier.fillMaxSize(), "♪", 72.sp)
                        }
                    }
                }
            }

            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(
                        tags["TITLE"] ?: track.title,
                        style = MaterialTheme.typography.headlineSmall,
                        color = cs.onBackground,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    val sub = listOf(tags["ARTIST"] ?: track.artist, tags["ALBUM"] ?: track.album).filter { it.isNotEmpty() }.joinToString("  •  ")
                    if (sub.isNotEmpty()) {
                        Text(
                            sub,
                            style = MaterialTheme.typography.bodyMedium,
                            color = cs.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
                IconTap(IconPaths.INFO, cs.onSurfaceVariant, 22.dp) { showInfo = true }
                IconTap(
                    if (isFavorite) IconPaths.HEART else IconPaths.HEART_OUTLINE,
                    if (isFavorite) cs.primary else cs.onBackground,
                    24.dp
                ) { onToggleFavorite() }
            }

            Row(
                Modifier.fillMaxWidth().padding(top = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                FormatBadge(track.format, false)
                info?.let {
                    Text(
                        it,
                        style = MaterialTheme.typography.labelMedium,
                        color = cs.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }

            SeekBar(
                value = positionMs.toFloat(),
                max = duration.toFloat(),
                onSeek = { onSeek(it.toLong()) },
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp)
            )
            Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp)) {
                Text(formatDuration(positionMs), style = MaterialTheme.typography.labelMedium, color = cs.onSurfaceVariant)
                Spacer(Modifier.weight(1f))
                Text(formatDuration(duration), style = MaterialTheme.typography.labelMedium, color = cs.onSurfaceVariant)
            }

            Row(
                Modifier.fillMaxWidth().padding(top = 8.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconTap(
                    IconPaths.SHUFFLE,
                    if (shuffle) cs.primary else cs.onSurfaceVariant,
                    22.dp, 10.dp
                ) { onToggleShuffle() }
                IconTap(IconPaths.PREV, cs.onBackground, 32.dp, 10.dp) { onPrev() }
                Box(
                    Modifier
                        .size(72.dp)
                        .clip(CircleShape)
                        .background(cs.primary)
                        .clickable { onPlayPause() },
                    contentAlignment = Alignment.Center
                ) {
                    PathIcon(if (isPlaying) IconPaths.PAUSE else IconPaths.PLAY, cs.onPrimary, 34.dp)
                }
                IconTap(IconPaths.NEXT, cs.onBackground, 32.dp, 10.dp) { onNext() }
                IconTap(
                    if (repeatMode == 2) IconPaths.REPEAT_ONE else IconPaths.REPEAT,
                    if (repeatMode > 0) cs.primary else cs.onSurfaceVariant,
                    22.dp, 10.dp
                ) { onToggleRepeat() }
            }

            if (track.isSlac) {
                Row(
                    Modifier
                        .padding(top = 16.dp)
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(18.dp))
                        .background(cs.surface.copy(alpha = 0.85f))
                        .clickable { showSpatial = true }
                        .padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        "Áudio espacial",
                        style = MaterialTheme.typography.titleSmall,
                        color = cs.onSurface,
                        modifier = Modifier.weight(1f)
                    )
                    Text(
                        "Largura ${(wideness * 100).toInt()}%  •  Reverb ${(reverbWet * 100).toInt()}%",
                        style = MaterialTheme.typography.labelMedium,
                        color = cs.primary
                    )
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }

    if (showSpatial) {
        ModalBottomSheet(
            onDismissRequest = { showSpatial = false },
            sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ) {
            Column(Modifier.padding(horizontal = 24.dp).padding(bottom = 32.dp)) {
                Text("Áudio espacial", style = MaterialTheme.typography.titleMedium)
                StereoField(
                    wideness, reverbWet,
                    Modifier.fillMaxWidth().height(150.dp).padding(vertical = 8.dp)
                )
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(PRESETS) { p ->
                        PillChip(p.name, abs(wideness - p.wideness) < 0.01f && abs(reverbWet - p.wet) < 0.01f, { onSpatialChange(p.wideness, p.wet) })
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text("Largura  ${(wideness * 100).toInt()}%", style = MaterialTheme.typography.labelMedium, color = cs.onSurfaceVariant)
                Slider(
                    value = wideness,
                    onValueChange = { onSpatialChange(it, reverbWet) },
                    valueRange = 0f..2f
                )
                Text("Reverb  ${(reverbWet * 100).toInt()}%", style = MaterialTheme.typography.labelMedium, color = cs.onSurfaceVariant)
                Slider(
                    value = reverbWet,
                    onValueChange = { onSpatialChange(wideness, it) },
                    valueRange = 0f..1f
                )
            }
        }
    }

    if (showInfo) {
        ModalBottomSheet(
            onDismissRequest = { showInfo = false },
            sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ) {
            Column(
                Modifier.verticalScroll(rememberScrollState()).padding(horizontal = 24.dp).padding(bottom = 32.dp)
            ) {
                Text("Informações da faixa", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                listOf(
                    "Título" to (tags["TITLE"] ?: track.title),
                    "Artista" to (tags["ARTIST"] ?: track.artist),
                    "Álbum" to (tags["ALBUM"] ?: track.album),
                    "Ano" to (tags["DATE"] ?: track.year),
                    "Gênero" to (tags["GENRE"] ?: track.genre),
                    "Faixa" to (tags["TRACK"] ?: track.trackNumber),
                    "Comentário" to (tags["COMMENT"] ?: track.comment),
                    "Formato" to track.format,
                    "Qualidade" to (info ?: ""),
                    "Bitrate" to if (track.bitrateKbps > 0) "${track.bitrateKbps} kbps" else "",
                    "Tamanho" to formatSize(track.sizeBytes)
                ).filter { it.second.isNotBlank() }.forEach { (k, v) -> InfoRow(k, v) }
                val known = setOf("TITLE", "ARTIST", "ALBUM", "DATE", "GENRE", "TRACK", "COMMENT")
                tags.filterKeys { it !in known }.forEach { (k, v) ->
                    InfoRow(k.lowercase().replaceFirstChar { it.uppercase() }, v)
                }
            }
        }
    }

    if (showQueue) {
        ModalBottomSheet(
            onDismissRequest = { showQueue = false },
            sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ) {
            Text(
                "Fila de reprodução",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 8.dp)
            )
            LazyColumn(contentPadding = PaddingValues(bottom = 32.dp)) {
                itemsIndexed(queue, key = { i, t -> "$i-${t.path}" }) { i, t ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clickable { showQueue = false; onSelectQueue(i) }
                            .padding(horizontal = 24.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                t.title,
                                style = MaterialTheme.typography.bodyMedium,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                color = if (i == queueIndex) cs.primary else cs.onSurface
                            )
                            Text(
                                "${t.artist.ifEmpty { t.album }}  •  ${formatDuration(t.durationMs)}",
                                style = MaterialTheme.typography.bodySmall,
                                color = cs.onSurfaceVariant,
                                maxLines = 1
                            )
                        }
                        if (i == queueIndex && isPlaying) EqBars()
                    }
                }
            }
        }
    }
}

/** Visualização do campo estéreo: alto-falantes se afastam com a largura; anéis crescem com o reverb. */
@Composable
private fun StereoField(wideness: Float, wet: Float, modifier: Modifier = Modifier) {
    val primary = MaterialTheme.colorScheme.primary
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    val w by animateFloatAsState(wideness, label = "w")
    val r by animateFloatAsState(wet, label = "r")
    Canvas(modifier) {
        val c = Offset(size.width / 2f, size.height / 2f)
        val maxR = size.height / 2f
        for (i in 1..3) {
            val radius = (maxR * (0.4f + 0.2f * i * (0.4f + r))).coerceAtMost(maxR * 1.6f)
            drawCircle(
                color = primary.copy(alpha = (0.06f + 0.3f * r) / i),
                radius = radius,
                center = c,
                style = Stroke(width = 2.dp.toPx())
            )
        }
        val spread = (0.12f + 0.36f * (w / 2f)) * size.width
        val left = Offset(c.x - spread, c.y)
        val right = Offset(c.x + spread, c.y)
        drawLine(primary.copy(alpha = 0.35f), left, c, strokeWidth = 2.dp.toPx())
        drawLine(primary.copy(alpha = 0.35f), right, c, strokeWidth = 2.dp.toPx())
        drawCircle(muted, radius = maxR * 0.16f, center = c)
        drawCircle(primary, radius = 9.dp.toPx(), center = left)
        drawCircle(primary, radius = 9.dp.toPx(), center = right)
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
        Text(
            label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.width(104.dp)
        )
        Text(value, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
    }
}