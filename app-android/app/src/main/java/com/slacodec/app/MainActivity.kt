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
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.runtime.Composable
import androidx.lifecycle.Lifecycle
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
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.slacodec.app.library.LibraryCache
import com.slacodec.app.library.LibraryScanner
import com.slacodec.app.library.SlacHeaderParser
import com.slacodec.app.model.Track
import com.slacodec.app.model.formatSampleRate
import com.slacodec.app.playback.PlayerManager
import com.slacodec.app.playback.SlacPlayer
import com.slacodec.app.ui.ArtLoader
import com.slacodec.app.ui.LibraryScreen
import com.slacodec.app.ui.MiniPlayer
import com.slacodec.app.ui.PlayerScreen
import com.slacodec.app.ui.SettingsScreen
import com.slacodec.app.ui.SlacodecTheme
import com.slacodec.app.ui.blurBitmap
import com.slacodec.app.ui.scaleMaxBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.coroutines.coroutineContext

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val ctx = LocalContext.current
            var themeMode by remember {
                mutableStateOf(ctx.getSharedPreferences("slac_prefs", MODE_PRIVATE).getInt("theme_mode", 0))
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

    // Sem permissão de mídia: o acesso vem da pasta escolhida (SAF)
    val hasPermission = true
    var tracks by remember { mutableStateOf<List<Track>>(emptyList()) }
    var cacheLoaded by remember { mutableStateOf(false) }
    var scanning by remember { mutableStateOf(false) }
    var scanJob by remember { mutableStateOf<Job?>(null) }
    var loadJob by remember { mutableStateOf<Job?>(null) }
    var currentIndex by remember { mutableStateOf<Int?>(null) }
    var isPlaying by remember { mutableStateOf(false) }
    var position by remember { mutableStateOf(0L) }
    var artFull by remember { mutableStateOf<Bitmap?>(null) }
    var artBlur by remember { mutableStateOf<Bitmap?>(null) }
    var wideness by remember { mutableStateOf(prefs.getFloat("def_wideness", 1.0f)) }
    var reverbWet by remember { mutableStateOf(prefs.getFloat("def_wet", 0.2f)) }
    var extraInfo by remember { mutableStateOf<String?>(null) }
    var trackTags by remember { mutableStateOf<Map<String, String>>(emptyMap()) }
    var favorites by remember { mutableStateOf(prefs.getStringSet("favs", emptySet()) ?: emptySet()) }
    var shuffle by remember { mutableStateOf(false) }
    var repeatMode by remember { mutableStateOf(0) }
    var folders by remember { mutableStateOf(LibraryScanner.savedTrees(context).toList()) }
    var themeModeLocal by remember { mutableStateOf(prefs.getInt("theme_mode", 0)) }
    var autoplay by remember { mutableStateOf(prefs.getBoolean("autoplay", true)) }
    var rescanOnOpen by remember { mutableStateOf(prefs.getBoolean("rescan_on_open", true)) }
    var useAuto by remember { mutableStateOf(prefs.getBoolean("use_auto", true)) }
    var defWideness by remember { mutableStateOf(prefs.getFloat("def_wideness", 1.0f)) }
    var defWet by remember { mutableStateOf(prefs.getFloat("def_wet", 0.2f)) }

    // ── Funcoes locais: ORDEM IMPORTA no Kotlin (sem referencia antecipada) ──

    fun rescan() {
        scanJob?.cancel()
        scanning = true
        scanJob = scope.launch {
            try {
                val known = tracks
                val fresh = withContext(Dispatchers.IO) {
                    // prioridade baixa: o scan não disputa CPU com animações e rolagem
                    val tid = android.os.Process.myTid()
                    val old = android.os.Process.getThreadPriority(tid)
                    android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND)
                    try { LibraryScanner.scan(context, known) } finally { android.os.Process.setThreadPriority(old) }
                }
                // preserva tags lidas enquanto o scan rodava
                val latest = tracks.associateBy { it.path }
                val merged = fresh.map { f ->
                    latest[f.path]?.takeIf {
                        it.tagsLoaded && it.sizeBytes == f.sizeBytes && it.lastModified == f.lastModified
                    } ?: f
                }
                if (merged != tracks) { // só recompõe a lista se algo mudou
                    tracks = merged
                    withContext(Dispatchers.IO) { LibraryCache.save(context, merged) }
                }
            } finally {
                if (scanJob === coroutineContext[Job]) scanning = false
            }
        }
    }

    // Recebe tags lidas sob demanda pela Biblioteca (troca só os itens afetados)
    fun mergeLoaded(loaded: List<Track>) {
        if (loaded.isEmpty()) return
        val byPath = loaded.associateBy { it.path }
        tracks = tracks.map { byPath[it.path] ?: it }
    }

    // Voltar seguro: ignora toques repetidos durante a animação e nunca remove a tela inicial
    // (dois "voltar" seguidos esvaziavam a pilha e deixavam a tela em branco).
    fun goBack() {
        val entry = nav.currentBackStackEntry ?: return
        if (nav.previousBackStackEntry != null && entry.lifecycle.currentState == Lifecycle.State.RESUMED) {
            nav.popBackStack()
        }
    }

    fun toggleFavorite(path: String) {
        val set = favorites.toMutableSet()
        if (!set.add(path)) set.remove(path)
        favorites = set
        prefs.edit().putStringSet("favs", set).apply()
    }

    fun selectTrack(index: Int) {
        val track = tracks.getOrNull(index) ?: return
        loadJob?.cancel()
        currentIndex = index
        isPlaying = false
        position = 0L
        trackTags = emptyMap() // evita mostrar tags da faixa anterior
        wideness = prefs.getFloat("def_wideness", 1.0f)
        reverbWet = prefs.getFloat("def_wet", 0.2f)
        // Navega na hora; o carregamento pesado acontece depois, fora da thread principal.
        if (nav.currentBackStackEntry?.destination?.route != "player") {
            nav.navigate("player") { launchSingleTop = true }
        }
        loadJob = scope.launch {
            val player = playerManager.loadFileAsync(context, track.path)
            player?.onPlaybackStateChanged = { playing -> isPlaying = playing }
            (player as? SlacPlayer)?.let {
                it.setUseAuto(useAuto)
                it.setSpatialParams(wideness, reverbWet)
            }
            if (autoplay) player?.play()
            val pair = withContext(Dispatchers.IO) {
                if (track.isSlac) {
                    val info = runCatching {
                        context.contentResolver.openInputStream(Uri.parse(track.path))?.use {
                            SlacHeaderParser.parse(it)
                        }
                    }.getOrNull()
                    trackTags = info?.tags ?: emptyMap()
                    extraInfo = info?.let {
                        listOfNotNull(
                            formatSampleRate(it.sampleRate).ifEmpty { null },
                            if (it.bits > 0) "${it.bits}-bit" else null,
                            when (it.channels) {
                                1 -> "Mono"
                                2 -> "Stereo"
                                else -> if (it.channels > 0) "${it.channels}ch" else null
                            }
                        ).joinToString("  •  ")
                    }
                    info?.coverBytes?.let { cb ->
                        BitmapFactory.decodeByteArray(cb, 0, cb.size)?.let { b ->
                            scaleMaxBitmap(b, 512) to
                                blurBitmap(Bitmap.createScaledBitmap(b, 96, 96, true), 6, 2)
                        }
                    }
                } else {
                    extraInfo = null
                    trackTags = emptyMap()
                    runCatching {
                        val r = MediaMetadataRetriever()
                        r.setDataSource(context, Uri.parse(track.path))
                        val bytes = r.embeddedPicture
                        r.release()
                        bytes?.let {
                            val bmp = BitmapFactory.decodeByteArray(it, 0, it.size)
                            bmp?.let { b ->
                                scaleMaxBitmap(b, 512) to
                                    blurBitmap(Bitmap.createScaledBitmap(b, 96, 96, true), 6, 2)
                            }
                        }
                    }.getOrNull()
                }
            }
            artFull = pair?.first
            artBlur = pair?.second
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

    fun handleEnded() {
        when (repeatMode) {
            2 -> {
                playerManager.getCurrentPlayer()?.seekTo(0)
                playerManager.getCurrentPlayer()?.play()
            }
            else -> {
                val idx = currentIndex ?: return
                val hasNext = shuffle || idx + 1 < tracks.size || repeatMode == 1
                if (hasNext) nextTrack()
                else {
                    playerManager.getCurrentPlayer()?.pause()
                    position = 0L
                }
            }
        }
    }

    fun togglePlayPause() {
        val p = playerManager.getCurrentPlayer() ?: return
        if (isPlaying) p.pause() else p.play()
    }

    // ── Launchers ──
    val treeLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let {
            // pasta única: a nova escolha substitui a anterior
            LibraryScanner.savedTrees(context).forEach { old -> LibraryScanner.removeTree(context, old) }
            LibraryScanner.addTree(context, it)
            folders = LibraryScanner.savedTrees(context).toList()
            rescan()
        }
    }

    // ── Effects (depois das funcoes que eles chamam) ──
    LaunchedEffect(Unit) {
        val cached = withContext(Dispatchers.IO) { LibraryCache.load(context) }
        if (tracks.isEmpty()) tracks = cached
        cacheLoaded = true
    }

    // Salva no cache (com debounce) as tags que foram sendo lidas
    LaunchedEffect(tracks) {
        if (cacheLoaded && tracks.isNotEmpty()) {
            delay(2000)
            withContext(Dispatchers.IO) { LibraryCache.save(context, tracks) }
        }
    }

    LaunchedEffect(hasPermission) { if (hasPermission) rescan() }

    val backStack by nav.currentBackStackEntryAsState()
    var firstRoute by remember { mutableStateOf(true) }
    LaunchedEffect(backStack?.destination?.route) {
        val route = backStack?.destination?.route
        if (route == "library" && !firstRoute && rescanOnOpen && hasPermission) {
            delay(500) // espera a animação de transição terminar
            rescan()
        }
        firstRoute = false
    }

    LaunchedEffect(currentIndex, isPlaying) {
        var ended = false
        while (isPlaying && !ended) {
            val p = playerManager.getCurrentPlayer()
            val pos = p?.positionMs() ?: 0L
            position = pos
            val dur = p?.durationMs() ?: 0L
            if (dur > 1000 && pos >= dur - 250) {
                ended = true
                handleEnded()
            } else {
                delay(400)
            }
        }
    }

    LaunchedEffect(wideness, reverbWet) {
        (playerManager.getCurrentPlayer() as? SlacPlayer)?.setSpatialParams(wideness, reverbWet)
    }

    DisposableEffect(Unit) { onDispose { playerManager.release() } }

    // ── Navegacao ──
    NavHost(
        navController = nav,
        startDestination = "library",
        enterTransition = { fadeIn(tween(300)) },
        exitTransition = { fadeOut(tween(200)) },
        popEnterTransition = { fadeIn(tween(300)) },
        popExitTransition = { fadeOut(tween(200)) }
    ) {
        composable(
            "library",
            exitTransition = {
                when (targetState.destination.route) {
                    "player" -> ExitTransition.None // o player sobe por cima
                    "settings" -> slideOutHorizontally(tween(320, easing = FastOutSlowInEasing)) { -it / 4 } +
                        fadeOut(tween(220))
                    else -> fadeOut(tween(200))
                }
            },
            popEnterTransition = {
                when (initialState.destination.route) {
                    "player" -> EnterTransition.None
                    "settings" -> slideInHorizontally(tween(320, easing = FastOutSlowInEasing)) { -it / 4 } +
                        fadeIn(tween(260))
                    else -> fadeIn(tween(300))
                }
            }
        ) {
            LibraryScreen(
                tracks = tracks,
                loading = scanning || !cacheLoaded,
                onTracksLoaded = { mergeLoaded(it) },
                currentPath = currentIndex?.let { tracks.getOrNull(it)?.path },
                isPlaying = isPlaying,
                onTrackClick = { t -> selectTrack(tracks.indexOf(t)) },
                onAddFolder = { treeLauncher.launch(null) },
                onSettings = { nav.navigate("settings") { launchSingleTop = true } },
                bottomBar = {
                    val t = currentIndex?.let { tracks.getOrNull(it) }
                    if (t != null) {
                        MiniPlayer(
                            track = t,
                            isPlaying = isPlaying,
                            art = artFull,
                            onPlayPause = { togglePlayPause() },
                            onNext = { nextTrack() },
                            onClick = { nav.navigate("player") { launchSingleTop = true } },
                            progress = { if (t.durationMs > 0) position.toFloat() / t.durationMs else 0f }
                        )
                    }
                }
            )
        }
        composable(
            "player",
            enterTransition = {
                slideInVertically(tween(400, easing = FastOutSlowInEasing)) { it } + fadeIn(tween(260))
            },
            popExitTransition = {
                slideOutVertically(tween(340, easing = FastOutSlowInEasing)) { it } + fadeOut(tween(260))
            }
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
                    tags = trackTags,
                    isFavorite = favorites.contains(track.path),
                    shuffle = shuffle,
                    repeatMode = repeatMode,
                    queue = tracks,
                    queueIndex = idx,
                    onSelectQueue = { i -> selectTrack(i) },
                    onBack = { goBack() },
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
        composable(
            "settings",
            enterTransition = {
                slideInHorizontally(tween(340, easing = FastOutSlowInEasing)) { it } + fadeIn(tween(260))
            },
            popExitTransition = {
                slideOutHorizontally(tween(320, easing = FastOutSlowInEasing)) { it } + fadeOut(tween(240))
            }
        ) {
            SettingsScreen(
                themeMode = themeModeLocal,
                onThemeMode = { m -> themeModeLocal = m; onThemeMode(m) },
                folders = folders,
                onRemoveFolder = { f ->
                    LibraryScanner.removeTree(context, f)
                    folders = LibraryScanner.savedTrees(context).toList()
                    rescan()
                },
                onAddFolder = { treeLauncher.launch(null) },
                onRescan = { rescan() },
                onBack = { goBack() },
                autoplay = autoplay,
                onAutoplay = { v -> autoplay = v; prefs.edit().putBoolean("autoplay", v).apply() },
                rescanOnOpen = rescanOnOpen,
                onRescanOnOpen = { v -> rescanOnOpen = v; prefs.edit().putBoolean("rescan_on_open", v).apply() },
                useAuto = useAuto,
                onUseAuto = { v ->
                    useAuto = v
                    prefs.edit().putBoolean("use_auto", v).apply()
                    (playerManager.getCurrentPlayer() as? SlacPlayer)?.setUseAuto(v)
                },
                defWideness = defWideness,
                defWet = defWet,
                onDefSpatial = { w, r ->
                    defWideness = w; defWet = r
                    prefs.edit().putFloat("def_wideness", w).putFloat("def_wet", r).apply()
                },
                onClearCache = {
                    ArtLoader.clear()
                    rescan()
                }
            )
        }
    }
}