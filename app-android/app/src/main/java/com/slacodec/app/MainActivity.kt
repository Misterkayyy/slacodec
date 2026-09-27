package com.slacodec.app

import android.Manifest
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.*
import com.slacodec.app.model.Track
import com.slacodec.app.library.LibraryScanner
import com.slacodec.app.playback.PlayerManager
import com.slacodec.app.playback.SlacPlayer
import com.slacodec.app.ui.LibraryScreen
import com.slacodec.app.ui.PlayerScreen
import com.slacodec.app.ui.SlacodecTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { SlacodecTheme { App() } }
    }
}

@Composable
fun App() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberCoroutineScope()
    val playerManager = remember { PlayerManager(context) }

    var hasPermission by remember { mutableStateOf(false) }
    var tracks by remember { mutableStateOf<List<Track>>(emptyList()) }
    var currentIndex by remember { mutableStateOf<Int?>(null) }
    var isPlaying by remember { mutableStateOf(false) }
    var position by remember { mutableStateOf(0L) }
    var art by remember { mutableStateOf<Bitmap?>(null) }
    var wideness by remember { mutableStateOf(1.0f) }
    var reverbWet by remember { mutableStateOf(0.2f) }

    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasPermission = granted }

    LaunchedEffect(Unit) {
        permLauncher.launch(
            if (Build.VERSION.SDK_INT >= 33) Manifest.permission.READ_MEDIA_AUDIO
            else Manifest.permission.READ_EXTERNAL_STORAGE
        )
    }

    // Scan da biblioteca
    LaunchedEffect(hasPermission) {
        if (!hasPermission) return@LaunchedEffect
        tracks = withContext(Dispatchers.IO) { LibraryScanner.scan() }
    }

    // Polling de posicao
    LaunchedEffect(currentIndex, isPlaying) {
        while (isPlaying) {
            position = playerManager.getCurrentPlayer()?.positionMs() ?: 0L
            delay(500)
        }
    }

    // Sliders espaciais em tempo real
    LaunchedEffect(wideness, reverbWet) {
        (playerManager.getCurrentPlayer() as? SlacPlayer)?.setSpatialParams(wideness, reverbWet)
    }

    fun selectTrack(index: Int) {
        scope.launch {
            currentIndex = index
            val track = tracks[index]
            playerManager.loadFile(context, track.path)
            playerManager.getCurrentPlayer()?.onPlaybackStateChanged = { playing -> isPlaying = playing }
            position = 0L
            // Extrai capa (formatos comuns); .slac usa placeholder por enquanto
            art = withContext(Dispatchers.IO) {
                if (track.isSlac) null else runCatching {
                    val r = MediaMetadataRetriever()
                    r.setDataSource(track.path)
                    val bytes = r.embeddedPicture
                    r.release()
                    bytes?.let {
                        val bmp = BitmapFactory.decodeByteArray(it, 0, it.size)
                        // Versao minuscula = blur natural quando escalada
                        bmp?.let { b -> Bitmap.createScaledBitmap(b, 48, 48, true) }
                    }
                }.getOrNull()
            }
        }
    }

    DisposableEffect(Unit) {
        onDispose { playerManager.release() }
    }

    val idx = currentIndex
    if (idx == null) {
        LibraryScreen(tracks = tracks) { t ->
            selectTrack(tracks.indexOf(t))
        }
    } else {
        val track = tracks[idx]
        PlayerScreen(
            track = track,
            art = art,
            isPlaying = isPlaying,
            positionMs = position,
            wideness = wideness,
            reverbWet = reverbWet,
            onBack = {
                playerManager.getCurrentPlayer()?.pause()
                currentIndex = null
            },
            onPlayPause = {
                val p = playerManager.getCurrentPlayer() ?: return@PlayerScreen
                if (isPlaying) p.pause() else p.play()
            },
            onNext = { if (idx + 1 < tracks.size) selectTrack(idx + 1) },
            onPrev = { if (idx - 1 >= 0) selectTrack(idx - 1) },
            onSeek = { ms ->
                playerManager.getCurrentPlayer()?.seekTo(ms)
                position = ms
            },
            onSpatialChange = { w, r -> wideness = w; reverbWet = r }
        )
    }
}
