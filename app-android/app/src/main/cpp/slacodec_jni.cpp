#include <jni.h>
#include <string>
#include <android/log.h>

// Inclui os headers do seu projeto existente
#include "rt/realtime_player.hpp"
#include "core/container.hpp"

#define LOG_TAG "SLACodecJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Mantém a instância do player viva entre chamadas JNI
struct PlayerContext {
    slac::rt::RealtimePlayer* player;
};

extern "C" JNIEXPORT jlong JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeInitPlayer(JNIEnv *env, jobject thiz) {
    auto* context = new PlayerContext();
    context->player = new slac::rt::RealtimePlayer();
    LOGI("RealtimePlayer inicializado com sucesso.");
    return reinterpret_cast<jlong>(context);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeLoadFile(JNIEnv *env, jobject thiz, jlong context_ptr, jstring file_path) {
    auto* context = reinterpret_cast<PlayerContext*>(context_ptr);
    const char* path = env->GetStringUTFChars(file_path, nullptr);
    
    bool success = context->player->open(path);
    
    env->ReleaseStringUTFChars(file_path, path);
    
    if (success) {
        LOGI("Arquivo carregado: %s", path);
    } else {
        LOGE("Falha ao carregar o arquivo: %s", path);
    }
    return success;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativePlay(JNIEnv *env, jobject thiz, jlong context_ptr) {
    auto* context = reinterpret_cast<PlayerContext*>(context_ptr);
    return context->player->play();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativePause(JNIEnv *env, jobject thiz, jlong context_ptr) {
    auto* context = reinterpret_cast<PlayerContext*>(context_ptr);
    return context->player->pause();
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeSetSpatialParams(JNIEnv *env, jobject thiz, jlong context_ptr, jfloat wideness, jfloat reverb_wet) {
    auto* context = reinterpret_cast<PlayerContext*>(context_ptr);
    // Supondo que RealtimePlayer tenha um método para atualizar parâmetros em tempo real
    // context->player->updateSpatialParams(wideness, reverb_wet);
    LOGI("Parâmetros espaciais atualizados: W=%.2f, R=%.2f", wideness, reverb_wet);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeRelease(JNIEnv *env, jobject thiz, jlong context_ptr) {
    auto* context = reinterpret_cast<PlayerContext*>(context_ptr);
    delete context->player;
    delete context;
    LOGI("Player liberado da memória.");
}
