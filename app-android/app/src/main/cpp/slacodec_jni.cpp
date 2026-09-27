#include <jni.h>
#include <string>
#include <cstdio>
#include <android/log.h>

#include "rt/realtime_player.hpp"
#include "rt/audio_engine.hpp"
#include "dsp/spatial_chain.hpp"

#define LOG_TAG "SLACodecJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
struct NativePlayer {
    slac::rt::RealtimePlayer player;
    slac::rt::AudioEngine engine;
    slac::dsp::SpatialChainConfig cfg;
    bool opened = false;
};
inline NativePlayer* P(jlong h) { return reinterpret_cast<NativePlayer*>(h); }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new NativePlayer());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeOpen(JNIEnv* env, jobject, jlong h, jstring path) {
    auto* np = P(h);
    const char* c = env->GetStringUTFChars(path, nullptr);
    std::string p(c);
    env->ReleaseStringUTFChars(path, c);

    np->cfg.wideness = 1.0f;
    np->cfg.reverb_enabled = false;
    np->cfg.use_auto = true;

    if (!np->player.open(p, np->cfg)) {
        LOGE("Falha ao abrir: %s", p.c_str());
        return JNI_FALSE;
    }
    np->opened = true;
    LOGI("Aberto: %s | sr=%u ch=%u total=%llu", p.c_str(),
         np->player.sample_rate(), np->player.channels(),
         (unsigned long long)np->player.total_samples());

    if (!np->engine.start(np->player.sample_rate(), 2, np->player.block_size(), &np->player)) {
        LOGE("AudioEngine falhou ao iniciar");
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativePlay(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    if (!np->opened) return JNI_FALSE;
    np->player.start();
    return np->engine.play() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeStop(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    np->engine.stop();
    np->player.stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeSetWideness(JNIEnv*, jobject, jlong h, jfloat w) {
    auto* np = P(h);
    np->cfg.wideness = w;
    if (np->opened) np->player.reconfigure(np->cfg);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeSetReverbWet(JNIEnv*, jobject, jlong h, jfloat wet) {
    auto* np = P(h);
    np->cfg.reverb.wet = wet;
    np->cfg.reverb_enabled = wet > 0.001f;
    if (np->opened) np->player.reconfigure(np->cfg);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeGetDurationMs(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    if (!np->opened || np->player.sample_rate() == 0) return 0;
    return (jlong)((np->player.total_samples() * 1000ULL) / np->player.sample_rate());
}

// Le apenas o header do container: duracao | sample_rate | canais | bits
extern "C" JNIEXPORT jstring JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeProbe(JNIEnv* env, jobject, jstring path) {
    const char* c = env->GetStringUTFChars(path, nullptr);
    std::string p(c);
    env->ReleaseStringUTFChars(path, c);

    slac::rt::StreamingDecoder dec;
    if (!slac::rt::StreamingDecoder::open(p, dec)) {
        return env->NewStringUTF("0|0|0|0");
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%llu|%u|%u|%d",
             (unsigned long long)dec.total_samples(),
             dec.sample_rate(), dec.channels(), dec.bits_per_sample());
    return env->NewStringUTF(buf);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeRelease(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    np->engine.stop();
    np->player.stop();
    delete np;
}
