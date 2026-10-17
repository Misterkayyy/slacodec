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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeSeek(JNIEnv*, jobject, jlong h, jlong positionMs) {
    auto* np = P(h);
    if (!np->opened || np->player.sample_rate() == 0) return JNI_FALSE;

    uint64_t target_sample = (static_cast<uint64_t>(positionMs) * np->player.sample_rate()) / 1000ULL;
    return np->player.seekToSample(target_sample) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeGetPositionMs(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    if (!np->opened || np->player.sample_rate() == 0) return 0;
    uint64_t current_sample = np->player.current_sample();
    return (jlong)((current_sample * 1000ULL) / np->player.sample_rate());
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeLoadHrir(JNIEnv* env, jobject, jlong h, jstring path) {
    auto* np = P(h);
    if (!np->opened) return JNI_FALSE;
    
    const char* c = env->GetStringUTFChars(path, nullptr);
    std::string p(c);
    env->ReleaseStringUTFChars(path, c);

    try {
        slac::dsp::WavData hw = slac::dsp::load_wav(p);
        auto chs = hw.channels();
        
        slac::dsp::TrueStereoIR ir;
        ir.sample_rate = hw.sample_rate;
        
        if (chs.size() >= 4) {
            ir.ll = chs[0]; ir.lr = chs[1]; ir.rl = chs[2]; ir.rr = chs[3];
        } else if (chs.size() == 2) {
            ir.ll = chs[0]; ir.rr = chs[1];
            ir.lr.assign(chs[0].size(), 0.0f);
            ir.rl.assign(chs[1].size(), 0.0f);
        } else {
            LOGE("HRIR precisa ter 2 ou 4 canais");
            return JNI_FALSE;
        }
        
        np->player.setExternalHrir(ir);
        LOGI("HRIR externa carregada com sucesso: %s", p.c_str());
        return JNI_TRUE;
        
    } catch (const std::exception& e) {
        LOGE("Falha ao carregar HRIR: %s", e.what());
        return JNI_FALSE;
    }
}

// Adicione após `nativeLoadHrir`:

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeHasEmbeddedHrir(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    if (!np->opened) return JNI_FALSE;
    
    const auto& emb = np->player.decoder().embedded_hrir();
    return emb.present() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeGetEmbeddedHrirType(JNIEnv* env, jobject, jlong h) {
    auto* np = P(h);
    if (!np->opened) return env->NewStringUTF("");
    
    const auto& emb = np->player.decoder().embedded_hrir();
    if (!emb.present()) return env->NewStringUTF("");
    
    const char* type_name = "Unknown";
    switch (emb.type) {
        case slac::dsp::HRIRType::TrueStereo: type_name = "True Stereo"; break;
        case slac::dsp::HRIRType::SurroundCrossfeed: type_name = "Surround Crossfeed"; break;
        case slac::dsp::HRIRType::SpatialIR: type_name = "Spatial IR"; break;
        default: break;
    }
    
    return env->NewStringUTF(type_name);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeSetUseAuto(JNIEnv*, jobject, jlong h, jboolean on) {
    auto* np = P(h);
    np->cfg.use_auto = (on == JNI_TRUE);
    if (np->opened) np->player.reconfigure(np->cfg);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_slacodec_app_codec_SlacodecJni_nativeGetDurationMs(JNIEnv*, jobject, jlong h) {
    auto* np = P(h);
    if (!np->opened || np->player.sample_rate() == 0) return 0;
    return (jlong)((np->player.total_samples() * 1000ULL) / np->player.sample_rate());
}

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