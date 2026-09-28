package com.slacodec.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    themeMode: Int,
    onThemeMode: (Int) -> Unit,
    folders: List<String>,
    onRemoveFolder: (String) -> Unit,
    onAddFolder: () -> Unit,
    onRescan: () -> Unit,
    onBack: () -> Unit,
    autoplay: Boolean,
    onAutoplay: (Boolean) -> Unit,
    rescanOnOpen: Boolean,
    onRescanOnOpen: (Boolean) -> Unit,
    useAuto: Boolean,
    onUseAuto: (Boolean) -> Unit,
    defWideness: Float,
    defWet: Float,
    onDefSpatial: (Float, Float) -> Unit,
    onClearCache: () -> Unit
) {
    Column(
        Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp, vertical = 12.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconTap(IconPaths.BACK, MaterialTheme.colorScheme.onBackground, 24.dp) { onBack() }
            Text("Ajustes", style = MaterialTheme.typography.headlineMedium)
        }

        Spacer(Modifier.padding(6.dp))

        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(18.dp)) {
            Column(Modifier.padding(16.dp)) {
                Text("Aparencia", style = MaterialTheme.typography.titleSmall)
                Spacer(Modifier.padding(4.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    listOf("Sistema" to 0, "Claro" to 1, "Escuro" to 2).forEach { (name, mode) ->
                        FilterChip(
                            selected = themeMode == mode,
                            onClick = { onThemeMode(mode) },
                            label = { Text(name, fontSize = 12.sp) }
                        )
                    }
                }
            }
        }

        Spacer(Modifier.padding(6.dp))

        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(18.dp)) {
            Column(Modifier.padding(16.dp)) {
                Text("Reproducao", style = MaterialTheme.typography.titleSmall)
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(Modifier.weight(1f)) {
                        Text("Tocar ao abrir", style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "Inicia a musica automaticamente ao selecionar",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(checked = autoplay, onCheckedChange = onAutoplay)
                }
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(Modifier.weight(1f)) {
                        Text("Atualizar ao abrir", style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "Re-escaneia a biblioteca toda vez que voltar a tela inicial",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(checked = rescanOnOpen, onCheckedChange = onRescanOnOpen)
                }
            }
        }

        Spacer(Modifier.padding(6.dp))

        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(18.dp)) {
            Column(Modifier.padding(16.dp)) {
                Text("Spatial (SLAC)", style = MaterialTheme.typography.titleSmall)
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(Modifier.weight(1f)) {
                        Text("Automacao do arquivo", style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "Usa os keyframes de automacao gravados no .slac",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(checked = useAuto, onCheckedChange = onUseAuto)
                }
                Text("Wideness padrao: ${"%.2f".format(defWideness)}", fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Slider(
                    value = defWideness,
                    onValueChange = { onDefSpatial(it, defWet) },
                    valueRange = 0f..2f
                )
                Text("Reverb Wet padrao: ${"%.2f".format(defWet)}", fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Slider(
                    value = defWet,
                    onValueChange = { onDefSpatial(defWideness, it) },
                    valueRange = 0f..1f
                )
            }
        }

        Spacer(Modifier.padding(6.dp))

        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(18.dp)) {
            Column(Modifier.padding(16.dp)) {
                Text("Biblioteca", style = MaterialTheme.typography.titleSmall)
                Spacer(Modifier.padding(4.dp))
                if (folders.isEmpty()) {
                    Text(
                        "Nenhuma pasta .slac adicionada.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    folders.forEach { f ->
                        Row(
                            Modifier.fillMaxWidth().padding(vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                f.substringAfterLast('/').ifEmpty { "Raiz" },
                                style = MaterialTheme.typography.bodyMedium,
                                modifier = Modifier.weight(1f)
                            )
                            IconTap(
                                IconPaths.CLOSE,
                                MaterialTheme.colorScheme.error,
                                18.dp, 8.dp
                            ) { onRemoveFolder(f) }
                        }
                    }
                }
                Spacer(Modifier.padding(4.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onAddFolder) { Text("Adicionar", fontSize = 12.sp) }
                    Button(onClick = onRescan) { Text("Re-escanear", fontSize = 12.sp) }
                    Button(onClick = onClearCache) { Text("Limpar cache", fontSize = 12.sp) }
                }
            }
        }

        Spacer(Modifier.padding(6.dp))

        Card(
            Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(18.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.primaryContainer
            )
        ) {
            Column(Modifier.padding(16.dp)) {
                Text(
                    "SLACodec Player",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                Text(
                    "Versao 1.1  •  Codec lossless LPC+Rice\n" +
                    "Container: fmt + meta + covr + spat + auto + seek\n" +
                    "Spatial engine: HRIR true-stereo + FDN reverb\n" +
                    "Playback: AAudio low-latency (21x realtime)\n" +
                    "Compilado com amor, direto do Termux.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                )
            }
        }
        Spacer(Modifier.padding(10.dp))
    }
}
