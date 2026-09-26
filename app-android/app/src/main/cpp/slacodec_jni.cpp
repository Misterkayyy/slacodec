#include <jni.h>
#include <string>
#include <android/log.h>

// Inclui usando o caminho relativo ao src/slac (definido no CMakeLists)
#include "rt/realtime_player.hpp"

#define LOG_TAG "SLACodecJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_slacodec_app_codec_SlacodecJni_getVersion(JNIEnv *env, jobject thiz) {
    LOGI("SLACodec JNI carregado com sucesso!");
    return env->NewStringUTF("1.0.0-native");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_testLoad(JNIEnv *env, jobject thiz, jstring file_path) {
    const char* path = env->GetStringUTFChars(file_path, nullptr);
    LOGI("Tentando carregar arquivo: %s", path);
    
    // Aqui você chamará o seu RealtimePlayer real. 
    // Por enquanto, retorna true apenas para validar o build.
    // Exemplo futuro: slac::rt::RealtimePlayer player; player.open(path);
    
    env->ReleaseStringUTFChars(file_path, path);
    return JNI_TRUE;
}
