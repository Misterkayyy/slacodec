package com.slacodec.app.ui

import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

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
    val cs = MaterialTheme.colorScheme
    Column(
        Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .verticalScroll(rememberScrollState())
    ) {
        Row(
            Modifier.fillMaxWidth().padding(start = 8.dp, end = 20.dp, top = 8.dp, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconTap(IconPaths.BACK, cs.onBackground, 22.dp, 10.dp) { onBack() }
            Text("Ajustes", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(start = 4.dp))
        }

        Column(Modifier.padding(horizontal = 16.dp)) {
            SectionLabel("Aparência")
            Group {
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Tema", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        listOf("Sistema" to 0, "Claro" to 1, "Escuro" to 2).forEach { (name, mode) ->
                            PillChip(name, themeMode == mode) { onThemeMode(mode) }
                        }
                    }
                }
            }

            SectionLabel("Reprodução")
            Group {
                SwitchRow("Tocar ao selecionar", "Inicia a música automaticamente", autoplay, onAutoplay)
                Divider()
                SwitchRow("Atualizar ao voltar", "Reescaneia a biblioteca ao retornar à tela inicial", rescanOnOpen, onRescanOnOpen)
            }

            SectionLabel("Áudio espacial (SLAC)")
            Group {
                SwitchRow("Automação do arquivo", "Usa os keyframes de automação gravados no .slac", useAuto, onUseAuto)
                Divider()
                SliderRow("Largura padrão", "%.2f".format(defWideness), defWideness, 0f..2f) { onDefSpatial(it, defWet) }
                Divider()
                SliderRow("Reverb padrão", "%.2f".format(defWet), defWet, 0f..1f) { onDefSpatial(defWideness, it) }
            }

            SectionLabel("Pasta da biblioteca")
            Group {
                if (folders.isEmpty()) {
                    Text(
                        "Nenhuma pasta selecionada.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = cs.onSurfaceVariant,
                        modifier = Modifier.padding(16.dp)
                    )
                } else {
                    folders.forEach { f ->
                        Row(
                            Modifier.fillMaxWidth().padding(start = 16.dp, end = 6.dp, top = 4.dp, bottom = 4.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                Uri.decode(f).substringAfterLast(':').substringAfterLast('/').ifEmpty { "Raiz" },
                                style = MaterialTheme.typography.bodyLarge,
                                modifier = Modifier.weight(1f)
                            )
                            IconTap(IconPaths.CLOSE, cs.error, 18.dp, 10.dp) { onRemoveFolder(f) }
                        }
                        Divider()
                    }
                }
                ActionRow(if (folders.isEmpty()) "Escolher pasta" else "Alterar pasta", onAddFolder)
                Divider()
                ActionRow("Reescanear pasta", onRescan)
                Divider()
                ActionRow("Limpar cache de capas", onClearCache)
            }

            Spacer(Modifier.height(28.dp))
            Text(
                "Kinetik Player",
                style = MaterialTheme.typography.titleSmall,
                color = cs.onSurface,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            Text(
                "Versão 1.0  •  Codec lossless LPC + Rice\n" +
                        "Container: fmt, meta, covr, spat, auto, seek\n" +
                        "Engine espacial: HRIR true-stereo + reverb FDN\n" +
                        "Reprodução: AAudio de baixa latência",
                style = MaterialTheme.typography.bodySmall,
                color = cs.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().padding(top = 6.dp)
            )
            Spacer(Modifier.height(24.dp).navigationBarsPadding())
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text.uppercase(),
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        letterSpacing = 1.2.sp,
        modifier = Modifier.padding(start = 8.dp, top = 24.dp, bottom = 8.dp)
    )
}

@Composable
private fun Group(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(MaterialTheme.colorScheme.surface),
        content = content
    )
}

@Composable
private fun Divider() {
    Box(
        Modifier
            .fillMaxWidth()
            .padding(start = 16.dp)
            .height(1.dp)
            .background(MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
    )
}

@Composable
private fun SwitchRow(title: String, subtitle: String, checked: Boolean, onChecked: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable { onChecked(!checked) }
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Spacer(Modifier.padding(6.dp))
        Switch(checked = checked, onCheckedChange = onChecked)
    }
}

@Composable
private fun SliderRow(
    label: String,
    valueText: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit
) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Text(valueText, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
        }
        Slider(value = value, onValueChange = onChange, valueRange = range)
    }
}

@Composable
private fun ActionRow(label: String, onClick: () -> Unit) {
    Text(
        label,
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 14.dp)
    )
}