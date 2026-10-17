package com.slacodec.app.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slacodec.app.model.Track

@Composable
fun MiniPlayer(
    track: Track,
    isPlaying: Boolean,
    art: Bitmap?,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onClick: () -> Unit,
    progress: () -> Float = { 0f }
) {
    val cs = MaterialTheme.colorScheme
    Surface(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
        shape = RoundedCornerShape(18.dp),
        color = cs.surface,
        tonalElevation = 2.dp,
        shadowElevation = 8.dp
    ) {
        Column {
            Row(
                Modifier.fillMaxWidth().clickable { onClick() }.padding(10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                val shape = RoundedCornerShape(10.dp)
                if (art != null) {
                    Image(
                        bitmap = art.asImageBitmap(),
                        contentDescription = null,
                        modifier = Modifier.size(44.dp).clip(shape),
                        contentScale = ContentScale.Crop,
                        filterQuality = FilterQuality.High
                    )
                } else {
                    GradientPlaceholder(Modifier.size(44.dp).clip(shape), "♪", 16.sp)
                }
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        track.title,
                        style = MaterialTheme.typography.titleSmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = cs.onSurface
                    )
                    Text(
                        track.artist.ifEmpty { track.album },
                        style = MaterialTheme.typography.bodySmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = cs.onSurfaceVariant
                    )
                }
                if (isPlaying) EqBars()
                Spacer(Modifier.width(6.dp))
                IconTap(if (isPlaying) IconPaths.PAUSE else IconPaths.PLAY, cs.onSurface, 24.dp, 8.dp) { onPlayPause() }
                IconTap(IconPaths.NEXT, cs.onSurface, 20.dp, 8.dp) { onNext() }
            }
            val trackColor = cs.onSurface.copy(alpha = 0.08f)
            val bar = cs.primary
            Box(
                Modifier.fillMaxWidth().height(2.dp).drawBehind {
                    drawRect(trackColor)
                    drawRect(bar, size = Size(size.width * progress().coerceIn(0f, 1f), size.height))
                }
            )
        }
    }
}