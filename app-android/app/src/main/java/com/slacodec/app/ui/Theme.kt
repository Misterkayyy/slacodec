package com.slacodec.app.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val BluePrimary = Color(0xFF2E93FF)

private val LightColors = lightColorScheme(
    primary = BluePrimary,
    onPrimary = Color.White,
    primaryContainer = Color(0xFFD6E9FF),
    onPrimaryContainer = Color(0xFF001D36),
    secondary = Color(0xFF546E7A),
    onSecondary = Color.White,
    background = Color(0xFFF7F9FC),
    onBackground = Color(0xFF101418),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF101418),
    surfaceVariant = Color(0xFFE3E8EF),
    onSurfaceVariant = Color(0xFF44474E)
)

private val DarkColors = darkColorScheme(
    primary = BluePrimary,
    onPrimary = Color.White,
    primaryContainer = Color(0xFF004A80),
    onPrimaryContainer = Color(0xFFD6E9FF),
    secondary = Color(0xFF90A4AE),
    onSecondary = Color(0xFF101418),
    background = Color(0xFF0E1420),
    onBackground = Color(0xFFE6EDF5),
    surface = Color(0xFF16202E),
    onSurface = Color(0xFFE6EDF5),
    surfaceVariant = Color(0xFF2A3644),
    onSurfaceVariant = Color(0xFFC4C7CE)
)

@Composable
fun SlacodecTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) DarkColors else LightColors,
        content = content
    )
}
