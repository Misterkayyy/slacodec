#include <jni.h>
#include <android/log.h>

// Agora o CMake sabe exatamente onde encontrar esta pasta 'rt'
#include "rt/realtime_player.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SLACodecJNI", __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_slacodec_app_codec_SlacodecJni_getVersion(JNIEnv *env, jobject thiz) {
    LOGI("SLACodec JNI compilado com sucesso! Caminho corrigido.");
    return env->NewStringUTF("1.0.0-path-fixed");
}
