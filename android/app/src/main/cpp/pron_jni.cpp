// Thin JNI layer over pron_c.h. All functions return JSON strings (or null on error).
#include <jni.h>

#include <string>
#include <vector>

#include "pron/pron_c.h"
#include "pron/pron_engine.h"

namespace {

std::string to_std(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);  // modified UTF-8; fine for BMP/ASCII text
    std::string r = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

jstring take_json(JNIEnv* env, char* s) {
    if (!s) return nullptr;
    jstring r = env->NewStringUTF(s);
    pron_free_string(s);
    return r;
}

pron_assessor* H(jlong h) { return reinterpret_cast<pron_assessor*>(h); }
pron_engine* E(jlong h) { return reinterpret_cast<pron_engine*>(h); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_app_englishpron_PronCore_nativeCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(pron_assessor_create());
}

JNIEXPORT void JNICALL Java_app_englishpron_PronCore_nativeDestroy(JNIEnv*, jclass, jlong h) {
    pron_assessor_destroy(H(h));
}

JNIEXPORT jstring JNICALL Java_app_englishpron_PronCore_nativeVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(pron_version());
}

JNIEXPORT jstring JNICALL Java_app_englishpron_PronCore_nativeLastError(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(pron_last_error(H(h)));
}

JNIEXPORT jint JNICALL Java_app_englishpron_PronCore_nativeLoadCmudict(JNIEnv* env, jclass, jlong h,
                                                                       jstring path) {
    return pron_assessor_load_cmudict_file(H(h), to_std(env, path).c_str());
}

JNIEXPORT jint JNICALL Java_app_englishpron_PronCore_nativeLoadCmudictText(JNIEnv* env, jclass, jlong h,
                                                                           jbyteArray data) {
    jsize n = env->GetArrayLength(data);
    std::vector<char> buf(static_cast<size_t>(n));
    if (n > 0) env->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(buf.data()));
    return pron_assessor_load_cmudict_text(H(h), buf.data(), buf.size());
}

JNIEXPORT jint JNICALL Java_app_englishpron_PronCore_nativeSetVocab(JNIEnv* env, jclass, jlong h,
                                                                    jobjectArray labels, jint blank) {
    jsize n = env->GetArrayLength(labels);
    std::vector<std::string> store(static_cast<size_t>(n));
    std::vector<const char*> ptrs(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        jstring s = static_cast<jstring>(env->GetObjectArrayElement(labels, i));
        store[i] = to_std(env, s);
        ptrs[i] = store[i].c_str();
        env->DeleteLocalRef(s);
    }
    return pron_assessor_set_phoneme_vocab(H(h), ptrs.data(), static_cast<int>(n), blank);
}

JNIEXPORT void JNICALL Java_app_englishpron_PronCore_nativeSetStrictness(JNIEnv*, jclass, jlong h, jint s) {
    pron_assessor_set_strictness(H(h), s);
}

JNIEXPORT jstring JNICALL Java_app_englishpron_PronCore_nativeTokenize(JNIEnv* env, jclass, jstring text) {
    return take_json(env, pron_tokenize(to_std(env, text).c_str()));
}

JNIEXPORT jstring JNICALL Java_app_englishpron_PronCore_nativeLookup(JNIEnv* env, jclass, jlong h,
                                                                     jstring word) {
    return take_json(env, pron_assessor_lookup(H(h), to_std(env, word).c_str()));
}

// words/starts/ends/probs describe the ASR result (may be empty); logPost may be null.
JNIEXPORT jstring JNICALL Java_app_englishpron_PronCore_nativeAssess(
    JNIEnv* env, jclass, jlong h, jstring reference, jobjectArray words, jdoubleArray starts,
    jdoubleArray ends, jfloatArray probs, jfloatArray logPost, jint nFrames, jint nClasses,
    jdouble frameSeconds) {
    const jsize n = words ? env->GetArrayLength(words) : 0;
    std::vector<std::string> texts(static_cast<size_t>(n));
    std::vector<pron_word> pw(static_cast<size_t>(n));
    std::vector<jdouble> st(static_cast<size_t>(n)), en(static_cast<size_t>(n));
    std::vector<jfloat> pr(static_cast<size_t>(n));
    if (n > 0) {
        env->GetDoubleArrayRegion(starts, 0, n, st.data());
        env->GetDoubleArrayRegion(ends, 0, n, en.data());
        env->GetFloatArrayRegion(probs, 0, n, pr.data());
    }
    for (jsize i = 0; i < n; ++i) {
        jstring s = static_cast<jstring>(env->GetObjectArrayElement(words, i));
        texts[i] = to_std(env, s);
        env->DeleteLocalRef(s);
        pw[i].text = texts[i].c_str();
        pw[i].start = st[i];
        pw[i].end = en[i];
        pw[i].probability = pr[i];
    }
    std::string ref = to_std(env, reference);
    std::vector<jfloat> post;
    if (logPost && nFrames > 0 && nClasses > 0) {
        post.resize(static_cast<size_t>(nFrames) * static_cast<size_t>(nClasses));
        env->GetFloatArrayRegion(logPost, 0, static_cast<jsize>(post.size()), post.data());
    }
    char* out = pron_assess(H(h), ref.c_str(), n > 0 ? pw.data() : nullptr, n,
                            post.empty() ? nullptr : post.data(), post.empty() ? 0 : nFrames,
                            post.empty() ? 0 : nClasses, frameSeconds);
    return take_json(env, out);
}

// ---- Real engine (pron_engine.h) ------------------------------------------------------------
JNIEXPORT jlong JNICALL Java_app_englishpron_engine_NativeEngine_engineCreate(JNIEnv* env, jclass, jstring dir,
                                                                              jint threads) {
    return reinterpret_cast<jlong>(pron_engine_create(to_std(env, dir).c_str(), threads));
}

JNIEXPORT void JNICALL Java_app_englishpron_engine_NativeEngine_engineDestroy(JNIEnv*, jclass, jlong h) {
    if (h) pron_engine_destroy(E(h));
}

JNIEXPORT jstring JNICALL Java_app_englishpron_engine_NativeEngine_engineStatus(JNIEnv* env, jclass, jlong h) {
    return take_json(env, pron_engine_status(E(h)));
}

JNIEXPORT jstring JNICALL Java_app_englishpron_engine_NativeEngine_engineLastError(JNIEnv* env, jclass, jlong h) {
    const char* e = pron_engine_last_error(E(h));
    return env->NewStringUTF(e ? e : "");
}

JNIEXPORT void JNICALL Java_app_englishpron_engine_NativeEngine_engineSetStrictness(JNIEnv*, jclass, jlong h,
                                                                                    jint s) {
    pron_engine_set_strictness(E(h), s);
}

JNIEXPORT jstring JNICALL Java_app_englishpron_engine_NativeEngine_engineAssessPcm16(
    JNIEnv* env, jclass, jlong h, jshortArray pcm, jint sampleRate, jstring reference) {
    const jsize n = pcm ? env->GetArrayLength(pcm) : 0;
    std::vector<jshort> buf(static_cast<size_t>(n));
    if (n > 0) env->GetShortArrayRegion(pcm, 0, n, buf.data());
    std::string ref = to_std(env, reference);
    char* out = pron_engine_assess_pcm16(E(h), reinterpret_cast<const int16_t*>(buf.data()),
                                         static_cast<size_t>(n), sampleRate, ref.c_str());
    return take_json(env, out);
}

// Returns [sampleRate, s0, s1, ...] or null on error.
JNIEXPORT jfloatArray JNICALL Java_app_englishpron_engine_NativeEngine_engineTts(
    JNIEnv* env, jclass, jlong h, jstring text, jstring voice, jfloat speed) {
    std::string t = to_std(env, text), v = to_std(env, voice);
    size_t count = 0;
    int rate = 0;
    float* a = pron_engine_tts(E(h), t.c_str(), v.c_str(), speed, &count, &rate);
    if (!a) return nullptr;
    jfloatArray r = env->NewFloatArray(static_cast<jsize>(count + 1));
    if (r) {
        const jfloat head = static_cast<jfloat>(rate);
        env->SetFloatArrayRegion(r, 0, 1, &head);
        if (count > 0) env->SetFloatArrayRegion(r, 1, static_cast<jsize>(count), a);
    }
    pron_engine_free_audio(a);
    return r;
}

JNIEXPORT jstring JNICALL Java_app_englishpron_engine_NativeEngine_engineLookup(JNIEnv* env, jclass, jlong h,
                                                                                jstring word) {
    return take_json(env, pron_engine_lookup(E(h), to_std(env, word).c_str()));
}

}  // extern "C"
