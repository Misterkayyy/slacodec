package com.slacodec.app.playback

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

class PlayerManager(private val context: Context) {
    private var currentPlayer: Player? = null

    /** Versão antiga (bloqueante). Prefira loadFileAsync. */
    fun loadFile(context: Context, uriString: String): Player? {
        currentPlayer?.release()
        val extension = uriString.substringAfterLast('.', "").lowercase()
        currentPlayer = if (extension == "slac") {
            SlacPlayer(context).also { it.loadFile(uriString) }
        } else {
            UniversalPlayer(context).also { it.loadFile(uriString) }
        }
        return currentPlayer
    }

    /**
     * Chame da thread principal. A parte pesada do SLAC (copiar o arquivo para o cache
     * e abrir no nativo) roda em IO. O ExoPlayer precisa ficar na thread principal.
     */
    suspend fun loadFileAsync(context: Context, uriString: String): Player? {
        val old = currentPlayer
        currentPlayer = null
        if (old is SlacPlayer) withContext(Dispatchers.IO) { old.release() } else old?.release()

        val extension = uriString.substringAfterLast('.', "").lowercase()
        val player: Player = if (extension == "slac") {
            val sp = withContext(Dispatchers.IO) { SlacPlayer(context).also { it.loadFile(uriString) } }
            if (!currentCoroutineContext().isActive) {
                sp.release()
                currentCoroutineContext().ensureActive()
            }
            sp
        } else {
            UniversalPlayer(context).also { it.loadFile(uriString) }
        }
        currentPlayer = player
        return player
    }

    fun getCurrentPlayer(): Player? = currentPlayer
    fun release() { currentPlayer?.release(); currentPlayer = null }
}