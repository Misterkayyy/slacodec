package com.slacodec.app.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

val BluePrimary = Color(0xFF2E93FF)

private val LightColors = lightColorScheme(
    primary = BluePrimary, onPrimary = Color.White,
    primaryContainer = Color(0xFFD6E9FF), onPrimaryContainer = Color(0xFF001D36),
    secondary = Color(0xFF546E7A), onSecondary = Color.White,
    secondaryContainer = Color(0xFFE3ECF5), onSecondaryContainer = Color(0xFF10243A),
    background = Color(0xFFF7F9FC), onBackground = Color(0xFF101418),
    surface = Color(0xFFFFFFFF), onSurface = Color(0xFF101418),
    surfaceVariant = Color(0xFFE3E8EF), onSurfaceVariant = Color(0xFF44474E)
)

private val DarkColors = darkColorScheme(
    primary = BluePrimary, onPrimary = Color.White,
    primaryContainer = Color(0xFF004A80), onPrimaryContainer = Color(0xFFD6E9FF),
    secondary = Color(0xFF90A4AE), onSecondary = Color(0xFF101418),
    secondaryContainer = Color(0xFF243447), onSecondaryContainer = Color(0xFFD6E9FF),
    background = Color(0xFF0E1420), onBackground = Color(0xFFE6EDF5),
    surface = Color(0xFF16202E), onSurface = Color(0xFFE6EDF5),
    surfaceVariant = Color(0xFF2A3644), onSurfaceVariant = Color(0xFFC4C7CE)
)

private val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(14.dp),
    large = RoundedCornerShape(20.dp),
    extraLarge = RoundedCornerShape(28.dp)
)

@Composable
fun SlacodecTheme(themeMode: Int = 0, content: @Composable () -> Unit) {
    val context = LocalContext.current
    val dark = when (themeMode) {
        1 -> false
        2 -> true
        else -> isSystemInDarkTheme()
    }
    val colorScheme = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        dark -> DarkColors
        else -> LightColors
    }
    MaterialTheme(colorScheme = colorScheme, shapes = AppShapes, content = content)
}
