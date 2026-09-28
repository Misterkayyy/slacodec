package com.slacodec.app.ui

import android.graphics.Bitmap
import androidx.compose.animation.animateContentSize
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
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
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
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatDuration
import kotlin.math.abs

private data class SpatialPreset(val name: String, val wideness: Float, val wet: Float)

private val PRESETS = listOf(
    SpatialPreset("Direto", 1.0f, 0.0f),
    SpatialPreset("Fones", 1.2f, 0.15f),
    SpatialPreset("Sala", 1.4f, 0.30f),
    SpatialPreset("Salao", 1.7f, 0.45f),
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
    val duration = track.durationMs.coerceAtLeast(1L)
    var spatialExpanded by remember { mutableStateOf(false) }
    var showQueue by remember { mutableStateOf(false) }
    val isDark = MaterialTheme.colorScheme.background.red < 0.5f
    val onBg = MaterialTheme.colorScheme.onBackground

    Box(Modifier.fillMaxSize()) {
        // Fundo: blur REAL (sem blocos)
        if (artBlur != null) {
            Image(
                bitmap = artBlur.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop,
                filterQuality = FilterQuality.High
            )
        } else {
            Box(
                Modifier.fillMaxSize().background(
                    Brush.verticalGradient(
                        listOf(
                            MaterialTheme.colorScheme.primaryContainer,
                            MaterialTheme.colorScheme.background
                        )
                    )
                )
            )
        }
        // Scrim adaptativo: escuro forte no dark mode (contraste total)
        Box(
            Modifier.fillMaxSize().background(
                MaterialTheme.colorScheme.background.copy(alpha = if (isDark) 0.72f else 0.42f)
            )
        )
        if (isDark) Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.25f)))

        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .statusBarsPadding()
                .padding(horizontal = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                IconTap(IconPaths.BACK, onBg, 24.dp) { onBack() }
                Spacer(Modifier.weight(1f))
                IconTap(IconPaths.QUEUE, onBg, 22.dp) { showQueue = true }
                IconTap(
                    if (isFavorite) IconPaths.HEART else IconPaths.HEART_OUTLINE,
                    if (isFavorite) MaterialTheme.colorScheme.primary else onBg,
                    22.dp
                ) { onToggleFavorite() }
                Spacer(Modifier.width(4.dp))
                if (track.isSlac) {
                    Surface(shape = RoundedCornerShape(8.dp), color = MaterialTheme.colorScheme.primary) {
                        Text(
                            "SLAC LOSSLESS",
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                            color = MaterialTheme.colorScheme.onPrimary,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }

            Spacer(Modifier.height(20.dp))

            Box(
                Modifier
                    .fillMaxWidth()
                    .aspectRatio(1f)
                    .clip(RoundedCornerShape(28.dp))
                    .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.9f))
                    .padding(14.dp)
            ) {
                Box(
                    Modifier.fillMaxSize().clip(RoundedCornerShape(18.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant),
                    contentAlignment = Alignment.Center
                ) {
                    if (art != null) {
                        Image(
                            bitmap = art.asImageBitmap(),
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

            Spacer(Modifier.height(22.dp))

            Text(
                track.title,
                style = MaterialTheme.typography.headlineSmall,
                color = onBg,
                maxLines = 1,
                textAlign = TextAlign.Center
            )
            if (track.artist.isNotEmpty()) {
                Text(
                    track.artist,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1
                )
            }
            Text(
                track.album + if (track.year.isNotEmpty()) "  •  ${track.year}" else "",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1
            )
            extraInfo?.let {
                Text(
                    it,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Bold
                )
            }

            Spacer(Modifier.height(16.dp))

            Slider(
                value = positionMs.toFloat().coerceIn(0f, duration.toFloat()),
                onValueChange = { onSeek(it.toLong()) },
                valueRange = 0f..duration.toFloat(),
                colors = SliderDefaults.colors(
                    thumbColor = MaterialTheme.colorScheme.primary,
                    activeTrackColor = MaterialTheme.colorScheme.primary
                ),
                modifier = Modifier.fillMaxWidth()
            )
            Row(Modifier.fillMaxWidth()) {
                Text(formatDuration(positionMs), fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.weight(1f))
                Text(formatDuration(duration), fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }

            Spacer(Modifier.height(8.dp))

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(36.dp)
            ) {
                IconTap(IconPaths.PREV, onBg, 30.dp, 10.dp) { onPrev() }
                IconTap(
                    if (isPlaying) IconPaths.PAUSE else IconPaths.PLAY,
                    MaterialTheme.colorScheme.primary,
                    56.dp, 12.dp
                ) { onPlayPause() }
                IconTap(IconPaths.NEXT, onBg, 30.dp, 10.dp) { onNext() }
            }

            Row(
                Modifier.fillMaxWidth().padding(horizontal = 56.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconTap(
                    IconPaths.SHUFFLE,
                    if (shuffle) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                    20.dp, 6.dp
                ) { onToggleShuffle() }
                Spacer(Modifier.weight(1f))
                IconTap(
                    if (repeatMode == 2) IconPaths.REPEAT_ONE else IconPaths.REPEAT,
                    if (repeatMode > 0) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                    20.dp, 6.dp
                ) { onToggleRepeat() }
            }

            if (track.isSlac) {
                Spacer(Modifier.height(8.dp))
                Card(
                    modifier = Modifier.fillMaxWidth().animateContentSize(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f)
                    ),
                    shape = RoundedCornerShape(18.dp)
                ) {
                    Column(Modifier.padding(horizontal = 16.dp, vertical = 10.dp)) {
                        Row(
                            Modifier.fillMaxWidth().clickable { spatialExpanded = !spatialExpanded },
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                "Spatial Processing",
                                style = MaterialTheme.typography.titleSmall,
                                color = MaterialTheme.colorScheme.onSurface,
                                modifier = Modifier.weight(1f)
                            )
                            Text(
                                if (spatialExpanded) "▾" else "▸",
                                color = MaterialTheme.colorScheme.primary
                            )
                        }
                        if (spatialExpanded) {
                            Spacer(Modifier.height(8.dp))
                            LazyRow(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                contentPadding = PaddingValues(bottom = 6.dp)
                            ) {
                                items(PRESETS) { p ->
                                    FilterChip(
                                        selected = abs(wideness - p.wideness) < 0.01f && abs(reverbWet - p.wet) < 0.01f,
                                        onClick = { onSpatialChange(p.wideness, p.wet) },
                                        label = { Text(p.name, fontSize = 12.sp) }
                                    )
                                }
                            }
                            Text("Wideness: ${"%.2f".format(wideness)}", fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Slider(
                                value = wideness,
                                onValueChange = { onSpatialChange(it, reverbWet) },
                                valueRange = 0f..2f,
                                colors = SliderDefaults.colors(
                                    thumbColor = MaterialTheme.colorScheme.primary,
                                    activeTrackColor = MaterialTheme.colorScheme.primary
                                )
                            )
                            Text("Reverb Wet: ${"%.2f".format(reverbWet)}", fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Slider(
                                value = reverbWet,
                                onValueChange = { onSpatialChange(wideness, it) },
                                valueRange = 0f..1f,
                                colors = SliderDefaults.colors(
                                    thumbColor = MaterialTheme.colorScheme.primary,
                                    activeTrackColor = MaterialTheme.colorScheme.primary
                                )
                            )
                        }
                    }
                }
            }

            Spacer(Modifier.height(24.dp))
            Spacer(Modifier.navigationBarsPadding())
        }
    }

    // Fila rapida (bottom sheet)
    if (showQueue) {
        ModalBottomSheet(
            onDismissRequest = { showQueue = false },
            sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ) {
            Text(
                "Fila de reproducao",
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
                                color = if (i == queueIndex) MaterialTheme.colorScheme.primary
                                        else MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                "${t.artist.ifEmpty { t.album }}  •  ${formatDuration(t.durationMs)}",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
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
