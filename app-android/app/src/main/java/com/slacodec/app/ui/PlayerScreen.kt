package com.slacodec.app.ui

import android.graphics.Bitmap
import android.os.Build
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatDuration
import kotlin.math.abs

private const val G_BACK = "←"
private const val G_PREV = "⏮\uFE0E"
private const val G_NEXT = "⏭\uFE0E"
private const val G_PLAY = "▶\uFE0E"
private const val G_PAUSE = "⏸\uFE0E"
private const val G_SHUFFLE = "⇄"
private const val G_REPEAT = "⟳"

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

    Box(Modifier.fillMaxSize()) {
        if (artBlur != null) {
            Image(
                bitmap = artBlur.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier
                    .fillMaxSize()
                    .then(if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Modifier.blur(24.dp) else Modifier),
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
        Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background.copy(alpha = 0.5f)))

        Column(
            Modifier
                .fillMaxSize()
                .statusBarsPadding()
                .padding(horizontal = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    G_BACK, fontSize = 26.sp,
                    color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier.clip(RoundedCornerShape(10.dp)).clickable { onBack() }.padding(8.dp)
                )
                Spacer(Modifier.weight(1f))
                Text(
                    if (isFavorite) "♥" else "♡",
                    fontSize = 22.sp,
                    color = if (isFavorite) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier.clip(RoundedCornerShape(10.dp)).clickable { onToggleFavorite() }.padding(8.dp)
                )
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

            Spacer(Modifier.weight(0.5f))

            Box(
                Modifier
                    .fillMaxWidth()
                    .aspectRatio(1f)
                    .clip(RoundedCornerShape(28.dp))
                    .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.85f))
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
                color = MaterialTheme.colorScheme.onBackground,
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

            Spacer(Modifier.height(4.dp))

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(36.dp)
            ) {
                Text(G_PREV, fontSize = 28.sp, color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier.clip(RoundedCornerShape(12.dp)).clickable { onPrev() }.padding(8.dp))
                Text(
                    if (isPlaying) G_PAUSE else G_PLAY,
                    fontSize = 46.sp,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.clip(RoundedCornerShape(16.dp)).clickable { onPlayPause() }.padding(10.dp)
                )
                Text(G_NEXT, fontSize = 28.sp, color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier.clip(RoundedCornerShape(12.dp)).clickable { onNext() }.padding(8.dp))
            }

            Row(
                Modifier.fillMaxWidth().padding(horizontal = 48.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    G_SHUFFLE, fontSize = 20.sp,
                    color = if (shuffle) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.clip(RoundedCornerShape(10.dp)).clickable { onToggleShuffle() }.padding(8.dp)
                )
                Spacer(Modifier.weight(1f))
                Text(
                    if (repeatMode == 2) "${G_REPEAT}¹" else G_REPEAT,
                    fontSize = 20.sp,
                    color = if (repeatMode > 0) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.clip(RoundedCornerShape(10.dp)).clickable { onToggleRepeat() }.padding(8.dp)
                )
            }

            if (track.isSlac) {
                Card(
                    modifier = Modifier.fillMaxWidth().animateContentSize(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.85f)
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

            Spacer(Modifier.weight(1f))
            Spacer(Modifier.height(12.dp))
        }
    }
}
