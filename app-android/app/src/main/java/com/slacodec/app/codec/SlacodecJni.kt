package com.slacodec.app.codec

class SlacodecJni {
    companion object {
        init {
            System.loadLibrary("slacodec_jni")
        }
    }

    // Método de teste para validar que o JNI está funcionando
    external fun getVersion(): String
    
    // Método de teste de carregamento
    external fun testLoad(filePath: String): Boolean
}
