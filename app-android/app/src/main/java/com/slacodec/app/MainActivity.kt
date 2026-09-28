package com.slacodec.app

import android.Manifest
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.slacodec.app.library.LibraryCache
import com.slacodec.app.library.LibraryScanner
import com.slacodec.app.library.SlacHeaderParser
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatSampleRate
import com.slacodec.app.playback.PlayerManager
import com.slacodec.app.playback.SlacPlayer
import com.slacodec.app.ui.LibraryScreen
import com.slacodec.app.ui.MiniPlayer
import com.slacodec.app.ui.PlayerScreen
import com.slacodec.app.ui.SettingsScreen
import com.slacodec.app.ui.SlacodecTheme
import com.slacodec.app.ui.scaleMaxBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val ctx = LocalContext.current
            var themeMode by remember {
                mutableStateOf(
                    ctx.getSharedPreferences("slac_prefs", MODE_PRIVATE).getInt("theme_mode", 0)
                )
            }
            SlacodecTheme(themeMode) {
                App(onThemeMode = { m ->
                    themeMode = m
                    ctx.getSharedPreferences("slac_prefs", MODE_PRIVATE)
                        .edit().putInt("theme_mode", m).apply()
                })
            }
        }
    }
}

@Composable
fun App(onThemeMode: (Int) -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val playerManager = remember { PlayerManager(context) }
    val nav = rememberNavController()
    val prefs = remember { context.getSharedPreferences("slac_prefs", 0) }

    var hasPermission by remember { mutableStateOf(false) }
    var tracks by remember { mutableStateOf(LibraryCache.load(context)) }
    var currentIndex by remember { mutableStateOf<Int?>(null) }
    var isPlaying by remember { mutableStateOf(false) }
    var position by remember { mutableStateOf(0L) }
    var artFull by remember { mutableStateOf<Bitmap?>(null) }
    var artBlur by remember { mutableStateOf<Bitmap?>(null) }
    var wideness by remember { mutableStateOf(1.0f) }
    var reverbWet by remember { mutableStateOf(0.2f) }
    var extraInfo by remember { mutableStateOf<String?>(null) }
    var favorites by remember { mutableStateOf(prefs.getStringSet("favs", emptySet()) ?: emptySet()) }
    var shuffle by remember { mutableStateOf(false) }
    var repeatMode by remember { mutableStateOf(0) }
    var folders by remember { mutableStateOf(LibraryScanner.savedTrees(context).toList()) }
    var themeModeLocal by remember { mutableStateOf(prefs.getInt("theme_mode", 0)) }

    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasPermission = granted }

    fun rescan() {
        scope.launch {
            val fresh = withContext(Dispatchers.IO) { LibraryScanner.scan(context) }
            tracks = fresh
            withContext(Dispatchers.IO) { LibraryCache.save(context, fresh) }
        }
    }

    val treeLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let {
            LibraryScanner.addTree(context, it)
            folders = LibraryScanner.savedTrees(context).toList()
            rescan()
        }
    }

    LaunchedEffect(Unit) {
        permLauncher.launch(
            if (Build.VERSION.SDK_INT >= 33) Manifest.permission.READ_MEDIA_AUDIO
            else Manifest.permission.READ_EXTERNAL_STORAGE
        )
    }

    LaunchedEffect(hasPermission) { if (hasPermission) rescan() }

    LaunchedEffect(currentIndex, isPlaying) {
        while (isPlaying) {
            position = playerManager.getCurrentPlayer()?.positionMs() ?: 0L
            delay(500)
        }
    }

    LaunchedEffect(wideness, reverbWet) {
        (playerManager.getCurrentPlayer() as? SlacPlayer)?.setSpatialParams(wideness, reverbWet)
    }

    fun toggleFavorite(path: String) {
        val set = favorites.toMutableSet()
        if (!set.add(path)) set.remove(path)
        favorites = set
        prefs.edit().putStringSet("favs", set).apply()
    }

    fun selectTrack(index: Int) {
        scope.launch {
            currentIndex = index
            val track = tracks.getOrNull(index) ?: return@launch
            playerManager.loadFile(context, track.path)
            playerManager.getCurrentPlayer()?.onPlaybackStateChanged = { playing -> isPlaying = playing }
            position = 0L
            val pair = withContext(Dispatchers.IO) {
                if (track.isSlac) {
                    val info = runCatching {
                        context.contentResolver.openInputStream(Uri.parse(track.path))?.use {
                            SlacHeaderParser.parse(it)
                        }
                    }.getOrNull()
                    extraInfo = info?.let {
                        listOfNotNull(
                            formatSampleRate(it.sampleRate).ifEmpty { null },
                            if (it.bits > 0) "${it.bits}-bit" else null,
                            when (it.channels) { 1 -> "Mono"; 2 -> "Stereo"; else -> if (it.channels > 0) "${it.channels}ch" else null }
                        ).joinToString("  •  ")
                    }
                    null
                } else {
                    extraInfo = null
                    runCatching {
                        val r = MediaMetadataRetriever()
                        r.setDataSource(context, Uri.parse(track.path))
                        val bytes = r.embeddedPicture
                        r.release()
                        bytes?.let {
                            val bmp = BitmapFactory.decodeByteArray(it, 0, it.size)
                            bmp?.let { b ->
                                scaleMaxBitmap(b, 512) to Bitmap.createScaledBitmap(b, 96, 96, true)
                            }
                        }
                    }.getOrNull()
                }
            }
            artFull = pair?.first
            artBlur = pair?.second
            playerManager.getCurrentPlayer()?.play()
            if (nav.currentBackStackEntry?.destination?.route != "player") {
                nav.navigate("player")
            }
        }
    }

    fun nextTrack() {
        val idx = currentIndex ?: return
        val n = tracks.size
        if (n == 0) return
        val target = when {
            shuffle -> (0 until n).random()
            idx + 1 < n -> idx + 1
            repeatMode == 1 -> 0
            else -> return
        }
        selectTrack(target)
    }

    fun prevTrack() {
        val idx = currentIndex ?: return
        if (idx - 1 >= 0) selectTrack(idx - 1)
    }

    fun togglePlayPause() {
        val p = playerManager.getCurrentPlayer() ?: return
        if (isPlaying) p.pause() else p.play()
    }

    DisposableEffect(Unit) { onDispose { playerManager.release() } }

    NavHost(navController = nav, startDestination = "library") {
        composable("library") {
            LibraryScreen(
                tracks = tracks,
                currentPath = currentIndex?.let { tracks.getOrNull(it)?.path },
                isPlaying = isPlaying,
                onTrackClick = { t -> selectTrack(tracks.indexOf(t)) },
                onAddFolder = { treeLauncher.launch(null) },
                onSettings = { nav.navigate("settings") },
                bottomBar = {
                    val t = currentIndex?.let { tracks.getOrNull(it) }
                    if (t != null) {
                        MiniPlayer(
                            track = t,
                            isPlaying = isPlaying,
                            art = artFull,
                            onPlayPause = { togglePlayPause() },
                            onNext = { nextTrack() },
                            onClick = { nav.navigate("player") }
                        )
                    }
                }
            )
        }
        composable(
            "player",
            enterTransition = { fadeIn(tween(220)) + slideInVertically(tween(280)) { it / 8 } },
            exitTransition = { fadeOut(tween(160)) },
            popEnterTransition = { fadeIn(tween(160)) },
            popExitTransition = { fadeOut(tween(220)) + slideOutVertically(tween(260)) { it / 8 } }
        ) {
            val idx = currentIndex
            val track = idx?.let { tracks.getOrNull(it) }
            if (track == null) {
                nav.popBackStack()
            } else {
                PlayerScreen(
                    track = track,
                    art = artFull,
                    artBlur = artBlur,
                    isPlaying = isPlaying,
                    positionMs = position,
                    wideness = wideness,
                    reverbWet = reverbWet,
                    extraInfo = extraInfo,
                    isFavorite = favorites.contains(track.path),
                    shuffle = shuffle,
                    repeatMode = repeatMode,
                    onBack = { nav.popBackStack() },
                    onPlayPause = { togglePlayPause() },
                    onNext = { nextTrack() },
                    onPrev = { prevTrack() },
                    onSeek = { ms ->
                        playerManager.getCurrentPlayer()?.seekTo(ms)
                        position = ms
                    },
                    onSpatialChange = { w, r -> wideness = w; reverbWet = r },
                    onToggleFavorite = { toggleFavorite(track.path) },
                    onToggleShuffle = { shuffle = !shuffle },
                    onToggleRepeat = { repeatMode = (repeatMode + 1) % 3 }
                )
            }
        }
        composable("settings") {
            SettingsScreen(
                themeMode = themeModeLocal,
                onThemeMode = { m ->
                    themeModeLocal = m
                    onThemeMode(m)
                },
                folders = folders,
                onRemoveFolder = { f ->
                    LibraryScanner.removeTree(context, f)
                    folders = LibraryScanner.savedTrees(context).toList()
                    rescan()
                },
                onAddFolder = { treeLauncher.launch(null) },
                onRescan = { rescan() },
                onBack = { nav.popBackStack() }
            )
        }
    }
}
