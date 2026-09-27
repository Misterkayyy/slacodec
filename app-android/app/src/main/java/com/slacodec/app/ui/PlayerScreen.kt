package com.slacodec.app.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatDuration

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PlayerScreen(
    track: Track,
    art: Bitmap?,
    isPlaying: Boolean,
    positionMs: Long,
    wideness: Float,
    reverbWet: Float,
    onBack: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrev: () -> Unit,
    onSeek: (Long) -> Unit,
    onSpatialChange: (Float, Float) -> Unit
) {
    val duration = track.durationMs.coerceAtLeast(1L)

    Box(Modifier.fillMaxSize()) {
        // ── Fundo: capa ofuscada preenchendo a tela ──
        if (art != null) {
            Image(
                bitmap = art.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
        } else {
            Box(
                Modifier
                    .fillMaxSize()
                    .background(
                        Brush.verticalGradient(
                            listOf(
                                MaterialTheme.colorScheme.primaryContainer,
                                MaterialTheme.colorScheme.background
                            )
                        )
                    )
            )
        }
        // Scrim para legibilidade
        Box(
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background.copy(alpha = 0.55f))
        )

        // ── Conteudo ──
        Column(
            modifier = Modifier
                .fillMaxSize()
                .statusBarsPadding()
                .padding(horizontal = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            // Top bar
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "←",
                    fontSize = 26.sp,
                    color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier
                        .clip(RoundedCornerShape(10.dp))
                        .clickable24 { onBack() }
                        .padding(8.dp)
                )
                Spacer(Modifier.weight(1f))
                if (track.isSlac) {
                    Surface(
                        shape = RoundedCornerShape(8.dp),
                        color = MaterialTheme.colorScheme.primary
                    ) {
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

            Spacer(Modifier.weight(0.6f))

            // ── Capa em moldura ──
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(1f)
                    .clip(RoundedCornerShape(28.dp))
                    .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.85f))
                    .padding(14.dp)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .clip(RoundedCornerShape(18.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant),
                    contentAlignment = Alignment.Center
                ) {
                    if (art != null) {
                        Image(
                            bitmap = art.asImageBitmap(),
                            contentDescription = null,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop
                        )
                    } else {
                        Text("♪", fontSize = 72.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }

            Spacer(Modifier.height(24.dp))

            // Titulo + album
            Text(
                track.title,
                style = MaterialTheme.typography.headlineSmall,
                color = MaterialTheme.colorScheme.onBackground,
                maxLines = 1,
                textAlign = TextAlign.Center
            )
            Text(
                "${track.album}  •  ${track.format}  •  ${track.bitrateKbps} kbps",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1
            )

            Spacer(Modifier.height(20.dp))

            // Seek bar
            Slider(
                value = positionMs.toFloat().coerceIn(0f, duration.toFloat()),
                onValueChange = { onSeek(it.toLong()) },
                valueRange = 0f..duration.toFloat(),
                modifier = Modifier.fillMaxWidth()
            )
            Row(Modifier.fillMaxWidth()) {
                Text(formatDuration(positionMs), fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.weight(1f))
                Text(formatDuration(duration), fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }

            Spacer(Modifier.height(8.dp))

            // Controles
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(32.dp)
            ) {
                Text("⏮", fontSize = 30.sp, color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier.clip(RoundedCornerShape(12.dp)).clickable24 { onPrev() }.padding(8.dp))
                Text(
                    if (isPlaying) "⏸" else "▶",
                    fontSize = 46.sp,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.clip(RoundedCornerShape(16.dp)).clickable24 { onPlayPause() }.padding(10.dp)
                )
                Text("⏭", fontSize = 30.sp, color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier.clip(RoundedCornerShape(12.dp)).clickable24 { onNext() }.padding(8.dp))
            }

            // ── Painel espacial (so .slac) ──
            if (track.isSlac) {
                Spacer(Modifier.height(16.dp))
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.8f)
                    ),
                    shape = RoundedCornerShape(18.dp)
                ) {
                    Column(Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                        Text(
                            "Spatial Processing",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text("Wideness: ${"%.2f".format(wideness)}", fontSize = 12.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Slider(
                            value = wideness,
                            onValueChange = { onSpatialChange(it, reverbWet) },
                            valueRange = 0f..2f
                        )
                        Text("Reverb Wet: ${"%.2f".format(reverbWet)}", fontSize = 12.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Slider(
                            value = reverbWet,
                            onValueChange = { onSpatialChange(wideness, it) },
                            valueRange = 0f..1f
                        )
                    }
                }
            }

            Spacer(Modifier.weight(1f))
            Spacer(Modifier.height(16.dp))
        }
    }
}

// Helper de clique sem ripple dependency
private fun Modifier.clickable24(onClick: () -> Unit): Modifier =
    this.then(androidx.compose.foundation.clickableModifier(onClick))

private fun androidx.compose.foundation.clickableModifier(onClick: () -> Unit): Modifier =
    androidx.compose.foundation.clickable(onClick = onClick)
