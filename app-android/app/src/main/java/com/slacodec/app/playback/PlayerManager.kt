package com.slacodec.app.playback

import android.content.Context

class PlayerManager(private val context: Context) {
    private var currentPlayer: Player? = null
    
    fun loadFile(context: Context, uriString: String): Player? {
        currentPlayer?.release()
        
        val extension = uriString.substringAfterLast('.', "").lowercase()
        
        currentPlayer = if (extension == "slac") {
            val p = SlacPlayer(context)
            p.loadFile(uriString)
            p
        } else {
            val p = UniversalPlayer(context)
            p.loadFile(uriString)
            p
        }
        return currentPlayer
    }
    
    fun getCurrentPlayer(): Player? = currentPlayer
    fun release() { currentPlayer?.release(); currentPlayer = null }
}
