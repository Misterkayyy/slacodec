package com.slacodec.app.ui

import android.app.Activity
import android.graphics.drawable.ColorDrawable
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Surface
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat

val BluePrimary = Color(0xFF5AA9FF)

private val LightColors = lightColorScheme(
    primary = Color(0xFF1566D6), onPrimary = Color.White,
    primaryContainer = Color(0xFFDCE9FF), onPrimaryContainer = Color(0xFF001C3D),
    secondary = Color(0xFF52606F), onSecondary = Color.White,
    secondaryContainer = Color(0xFFE8EDF5), onSecondaryContainer = Color(0xFF0E1A28),
    background = Color(0xFFF4F6FA), onBackground = Color(0xFF0D1218),
    surface = Color(0xFFFFFFFF), onSurface = Color(0xFF0D1218),
    surfaceVariant = Color(0xFFE6EAF1), onSurfaceVariant = Color(0xFF5B6574),
    outline = Color(0xFFB9C1CE), outlineVariant = Color(0xFFDDE2EA),
    error = Color(0xFFD93025)
)

private val DarkColors = darkColorScheme(
    primary = BluePrimary, onPrimary = Color(0xFF06182E),
    primaryContainer = Color(0xFF16314F), onPrimaryContainer = Color(0xFFD6E8FF),
    secondary = Color(0xFF9AA8B8), onSecondary = Color(0xFF0D1218),
    secondaryContainer = Color(0xFF1B2330), onSecondaryContainer = Color(0xFFE3E9F1),
    background = Color(0xFF0B0D12), onBackground = Color(0xFFECEFF4),
    surface = Color(0xFF14181F), onSurface = Color(0xFFECEFF4),
    surfaceVariant = Color(0xFF1E2430), onSurfaceVariant = Color(0xFF9AA4B2),
    outline = Color(0xFF3A4352), outlineVariant = Color(0xFF262D39),
    error = Color(0xFFFF6B6B)
)

private val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(22.dp),
    extraLarge = RoundedCornerShape(32.dp)
)

private val AppTypography = Typography(
    headlineMedium = TextStyle(fontSize = 30.sp, lineHeight = 36.sp, fontWeight = FontWeight.Bold, letterSpacing = (-0.5).sp),
    headlineSmall = TextStyle(fontSize = 24.sp, lineHeight = 30.sp, fontWeight = FontWeight.Bold, letterSpacing = (-0.3).sp),
    titleMedium = TextStyle(fontSize = 16.sp, lineHeight = 22.sp, fontWeight = FontWeight.SemiBold),
    titleSmall = TextStyle(fontSize = 14.sp, lineHeight = 20.sp, fontWeight = FontWeight.SemiBold),
    bodyLarge = TextStyle(fontSize = 16.sp, lineHeight = 22.sp, fontWeight = FontWeight.Medium),
    bodyMedium = TextStyle(fontSize = 14.sp, lineHeight = 20.sp),
    bodySmall = TextStyle(fontSize = 12.sp, lineHeight = 16.sp),
    labelMedium = TextStyle(fontSize = 12.sp, lineHeight = 16.sp, fontWeight = FontWeight.Medium),
    labelSmall = TextStyle(fontSize = 11.sp, lineHeight = 14.sp, fontWeight = FontWeight.Medium)
)

/**
 * Tema do app. Além das cores, cuida de tudo que fazia o claro/escuro falhar:
 * fundo raiz, fundo da janela e ícones das barras do sistema seguem o modo escolhido
 * no app (0 = sistema, 1 = claro, 2 = escuro), não só o do sistema.
 */
@Composable
fun SlacodecTheme(themeMode: Int = 0, content: @Composable () -> Unit) {
    val dark = when (themeMode) {
        1 -> false
        2 -> true
        else -> isSystemInDarkTheme()
    }
    val scheme = if (dark) DarkColors else LightColors
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            WindowCompat.setDecorFitsSystemWindows(window, false)
            window.statusBarColor = Color.Transparent.toArgb()
            window.navigationBarColor = Color.Transparent.toArgb()
            window.setBackgroundDrawable(ColorDrawable(scheme.background.toArgb()))
            if (Build.VERSION.SDK_INT >= 29) window.isNavigationBarContrastEnforced = false
            val controller = WindowCompat.getInsetsController(window, view)
            controller.isAppearanceLightStatusBars = !dark
            controller.isAppearanceLightNavigationBars = !dark
        }
    }
    MaterialTheme(colorScheme = scheme, shapes = AppShapes, typography = AppTypography) {
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = scheme.background,
            contentColor = scheme.onBackground,
            content = content
        )
    }
}