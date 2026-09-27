package com.slacodec.app.playback

import android.content.Context
import android.net.Uri
import com.slacodec.app.codec.SlacodecJni
import java.io.File
import java.io.FileOutputStream

class SlacPlayer(private val context: Context) : Player {
    private val jni = SlacodecJni()
    private var cachedFile: File? = null
    override var onPlaybackStateChanged: ((Boolean) -> Unit)? = null
    
    fun loadFile(uriString: String): Boolean {
        return try {
            val uri = Uri.parse(uriString)
            // Copia o arquivo da URI para o cache interno do app
            cachedFile = copyUriToCache(uri)
            // Passa o CAMINHO REAL do arquivo para o C++ (não a URI!)
            jni.testLoad(cachedFile!!.absolutePath)
        } catch (e: Exception) {
            false
        }
    }
    
    private fun copyUriToCache(uri: Uri): File {
        // Cria um arquivo único no cache para cada música
        val cachedFile = File(context.cacheDir, "current.slac")
        if (cachedFile.exists()) cachedFile.delete()
        
        context.contentResolver.openInputStream(uri)?.use { input ->
            FileOutputStream(cachedFile).use { output ->
                input.copyTo(output)
            }
        }
        return cachedFile
    }
    
    override fun play() { onPlaybackStateChanged?.invoke(true) }
    override fun pause() { onPlaybackStateChanged?.invoke(false) }
    override fun release() { 
        cachedFile?.delete()
    }
    
    fun setSpatialParams(wideness: Float, reverbWet: Float) {
        // TODO: Conectar ao C++ real
    }
}
