package com.slacodec.app.ui

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun FormatBadge(format: String, highlighted: Boolean) {
    Surface(
        shape = RoundedCornerShape(6.dp),
        color = if (highlighted) MaterialTheme.colorScheme.primary
        else MaterialTheme.colorScheme.surfaceVariant
    ) {
        Text(
            format,
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 3.dp),
            color = if (highlighted) MaterialTheme.colorScheme.onPrimary
            else MaterialTheme.colorScheme.onSurfaceVariant,
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

/** Chip em pílula usado em filtros, presets e seletor de tema. */
@Composable
fun PillChip(label: String, selected: Boolean, onClick: () -> Unit) {
    val cs = MaterialTheme.colorScheme
    Box(
        Modifier
            .clip(RoundedCornerShape(50))
            .background(if (selected) cs.primary else cs.surfaceVariant)
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 7.dp)
    ) {
        Text(
            label,
            fontSize = 13.sp,
            fontWeight = FontWeight.Medium,
            color = if (selected) cs.onPrimary else cs.onSurfaceVariant
        )
    }
}

/** Barra de progresso fina com arrasto; só envia o seek ao soltar. */
@Composable
fun SeekBar(value: Float, max: Float, onSeek: (Float) -> Unit, modifier: Modifier = Modifier) {
    val cs = MaterialTheme.colorScheme
    var dragging by remember { mutableStateOf(false) }
    var dragFrac by remember { mutableStateOf(0f) }
    val frac = if (dragging) dragFrac else if (max > 0f) (value / max).coerceIn(0f, 1f) else 0f
    val thumbR by animateFloatAsState(if (dragging) 9f else 6f, label = "thumb")
    val trackColor = cs.onSurface.copy(alpha = 0.14f)
    val active = cs.primary
    Canvas(
        modifier
            .height(28.dp)
            .pointerInput(max) {
                detectTapGestures { o -> onSeek((o.x / size.width).coerceIn(0f, 1f) * max) }
            }
            .pointerInput(max) {
                detectHorizontalDragGestures(
                    onDragStart = { o ->
                        dragging = true
                        dragFrac = (o.x / size.width).coerceIn(0f, 1f)
                    },
                    onDragEnd = { dragging = false; onSeek(dragFrac * max) },
                    onDragCancel = { dragging = false },
                    onHorizontalDrag = { change, _ ->
                        dragFrac = (change.position.x / size.width).coerceIn(0f, 1f)
                    }
                )
            }
    ) {
        val h = 4.dp.toPx()
        val y = size.height / 2f
        val inset = 8.dp.toPx()
        val w = size.width - inset * 2
        drawRoundRect(trackColor, Offset(inset, y - h / 2), Size(w, h), CornerRadius(h / 2))
        drawRoundRect(active, Offset(inset, y - h / 2), Size(w * frac, h), CornerRadius(h / 2))
        drawCircle(active, thumbR.dp.toPx(), Offset(inset + w * frac, y))
    }
}

@Composable
fun EqBars(color: Color = MaterialTheme.colorScheme.primary) {
    val transition = rememberInfiniteTransition(label = "eq")
    Row(
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalAlignment = Alignment.Bottom,
        modifier = Modifier.height(16.dp)
    ) {
        listOf(0, 1, 2).forEach { i ->
            val fraction by transition.animateFloat(
                initialValue = 0.3f,
                targetValue = 1f,
                animationSpec = infiniteRepeatable(
                    tween(420 + i * 160, easing = FastOutSlowInEasing),
                    repeatMode = RepeatMode.Reverse
                ),
                label = "bar$i"
            )
            Box(
                Modifier
                    .width(3.dp)
                    .height((16 * fraction).dp)
                    .clip(RoundedCornerShape(2.dp))
                    .background(color)
            )
        }
    }
}

@Composable
fun GradientPlaceholder(
    modifier: Modifier = Modifier,
    glyph: String = "♪",
    glyphSize: TextUnit = 20.sp
) {
    Box(
        modifier.background(
            Brush.linearGradient(
                listOf(
                    MaterialTheme.colorScheme.primaryContainer,
                    MaterialTheme.colorScheme.surfaceVariant
                )
            )
        ),
        contentAlignment = Alignment.Center
    ) {
        Text(glyph, color = MaterialTheme.colorScheme.onPrimaryContainer, fontSize = glyphSize)
    }
}