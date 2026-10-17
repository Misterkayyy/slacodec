package com.slacodec.app.library

import android.content.Context
import com.slacodec.app.model.Track
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext

/** Lê as tags de uma faixa sob demanda (no máximo 2 leituras ao mesmo tempo, prioridade baixa). */
object TagLoader {
    private val gate = Semaphore(2)

    suspend fun load(context: Context, track: Track): Track =
        gate.withPermit {
            withContext(Dispatchers.IO) {
                val tid = android.os.Process.myTid()
                val old = android.os.Process.getThreadPriority(tid)
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND)
                try {
                    LibraryScanner.readMetadata(context, track)
                } finally {
                    android.os.Process.setThreadPriority(old)
                }
            }
        }
}