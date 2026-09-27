package com.slacodec.app

import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.slacodec.app.playback.PlayerManager
import com.slacodec.app.playback.SlacPlayer
import com.slacodec.app.ui.SlacodecTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            SlacodecTheme { MainScreen() }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen() {
    val context = LocalContext.current
    val playerManager = remember { PlayerManager(context) }

    var currentFileName by remember { mutableStateOf<String?>(null) }
    var isPlaying by remember { mutableStateOf(false) }
    var wideness by remember { mutableStateOf(1.0f) }
    var reverbWet by remember { mutableStateOf(0.2f) }

    // OpenDocument aceita MULTIPLOS MIME types (audio/* + binarios .slac)
    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let {
            currentFileName = it.lastPathSegment?.substringAfterLast('/') ?: "Audio"
            playerManager.loadFile(context, it.toString())
        }
    }

    LaunchedEffect(currentFileName) {
        playerManager.getCurrentPlayer()?.onPlaybackStateChanged = { playing -> isPlaying = playing }
    }

    LaunchedEffect(wideness, reverbWet) {
        (playerManager.getCurrentPlayer() as? SlacPlayer)?.setSpatialParams(wideness, reverbWet)
    }

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            modifier = Modifier.fillMaxSize().padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text("SLACodec Player", style = MaterialTheme.typography.headlineMedium)

            Button(onClick = {
                filePickerLauncher.launch(arrayOf("audio/*", "application/octet-stream"))
            }) {
                Text("Selecionar Arquivo de Audio")
            }

            currentFileName?.let { name ->
                Text("Arquivo: $name", style = MaterialTheme.typography.bodyMedium)
            }

            Button(
                onClick = {
                    if (isPlaying) playerManager.getCurrentPlayer()?.pause()
                    else playerManager.getCurrentPlayer()?.play()
                },
                enabled = currentFileName != null
            ) {
                Text(if (isPlaying) "Pause" else "Play")
            }

            if (currentFileName?.endsWith(".slac") == true) {
                Card(modifier = Modifier.fillMaxWidth(), elevation = CardDefaults.cardElevation(4.dp)) {
                    Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("Controles Espaciais (SLAC)", style = MaterialTheme.typography.titleMedium)

                        Text("Wideness: ${"%.2f".format(wideness)}")
                        Slider(value = wideness, onValueChange = { wideness = it }, valueRange = 0f..2f)

                        Text("Reverb Wet: ${"%.2f".format(reverbWet)}")
                        Slider(value = reverbWet, onValueChange = { reverbWet = it }, valueRange = 0f..1f)
                    }
                }
            }
        }
    }
}
