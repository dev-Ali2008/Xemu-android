#include <jni.h>

#include <android/log.h>
#include <array>
#include <string>

#define XISO_LOG_TAG "XisoConverter"

extern "C" int xiso_convert_iso_to_xiso(const char *input_path,
                                          const char *output_path,
                                          char *err_buf,
                                          size_t err_buf_len);

extern "C" int xiso_convert_iso_to_xiso_with_progress(
    const char *input_path,
    const char *output_path,
    char *err_buf,
    size_t err_buf_len,
    void (*progress_callback)(int current, int total, void *user_data),
    void *user_data);

struct JniProgressContext {
    JNIEnv *env;
    jobject callback;
    jmethodID method_id;
    bool failed;
};

static void xiso_progress_bridge(int current, int total, void *user_data) {
    auto *ctx = static_cast<JniProgressContext *>(user_data);
    if (ctx->failed) return;
    if (ctx->env->ExceptionCheck()) {
        ctx->failed = true;
        ctx->env->ExceptionClear();
        return;
    }
    ctx->env->CallVoidMethod(ctx->callback, ctx->method_id,
                             static_cast<jint>(current),
                             static_cast<jint>(total));
    if (ctx->env->ExceptionCheck()) {
        ctx->failed = true;
        ctx->env->ExceptionClear();
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_Ali_Xanite_XisoConverterNative_nativeConvertIsoToXiso(
    JNIEnv *env,
    jclass,
    jstring input_path,
    jstring output_path) {
  if (input_path == nullptr || output_path == nullptr) {
    return env->NewStringUTF("Input/output path is missing");
  }

  const char *input_chars = env->GetStringUTFChars(input_path, nullptr);
  if (input_chars == nullptr) {
    return env->NewStringUTF("Failed to read input path");
  }
  const char *output_chars = env->GetStringUTFChars(output_path, nullptr);
  if (output_chars == nullptr) {
    env->ReleaseStringUTFChars(input_path, input_chars);
    return env->NewStringUTF("Failed to read output path");
  }

  std::array<char, 4096> error_buffer{};
  int rc = xiso_convert_iso_to_xiso(
      input_chars,
      output_chars,
      error_buffer.data(),
      error_buffer.size());

  env->ReleaseStringUTFChars(input_path, input_chars);
  env->ReleaseStringUTFChars(output_path, output_chars);

  if (rc == 0) {
    return nullptr;
  }

  const char *msg = error_buffer[0] != '\0'
                        ? error_buffer.data()
                        : "ISO conversion failed";
  __android_log_print(ANDROID_LOG_ERROR, XISO_LOG_TAG,
                      "nativeConvertIsoToXiso rc=%d: %s", rc, msg);
  return env->NewStringUTF(msg);
}

extern "C" JNIEXPORT jstring JNICALL
Java_Ali_Xanite_XisoConverterNative_nativeConvertIsoToXisoWithProgress(
    JNIEnv *env,
    jclass,
    jstring input_path,
    jstring output_path,
    jobject progress_callback) {
  if (input_path == nullptr || output_path == nullptr) {
    return env->NewStringUTF("Input/output path is missing");
  }

  const char *input_chars = env->GetStringUTFChars(input_path, nullptr);
  if (input_chars == nullptr) {
    return env->NewStringUTF("Failed to read input path");
  }
  const char *output_chars = env->GetStringUTFChars(output_path, nullptr);
  if (output_chars == nullptr) {
    env->ReleaseStringUTFChars(input_path, input_chars);
    return env->NewStringUTF("Failed to read output path");
  }

  std::array<char, 4096> error_buffer{};
  int rc;

  if (progress_callback != nullptr) {
    jclass cb_class = env->GetObjectClass(progress_callback);
    jmethodID on_progress = env->GetMethodID(cb_class, "onProgress", "(II)V");

    if (on_progress == nullptr) {
      env->ReleaseStringUTFChars(input_path, input_chars);
      env->ReleaseStringUTFChars(output_path, output_chars);
      return env->NewStringUTF("Failed to find onProgress method on callback");
    }

    JniProgressContext ctx{env, progress_callback, on_progress, false};
    rc = xiso_convert_iso_to_xiso_with_progress(
        input_chars,
        output_chars,
        error_buffer.data(),
        error_buffer.size(),
        xiso_progress_bridge,
        &ctx);
  } else {
    rc = xiso_convert_iso_to_xiso(
        input_chars,
        output_chars,
        error_buffer.data(),
        error_buffer.size());
  }

  env->ReleaseStringUTFChars(input_path, input_chars);
  env->ReleaseStringUTFChars(output_path, output_chars);

  if (rc == 0) {
    return nullptr;
  }

  const char *msg = error_buffer[0] != '\0'
                        ? error_buffer.data()
                        : "ISO conversion failed";
  __android_log_print(ANDROID_LOG_ERROR, XISO_LOG_TAG,
                      "nativeConvertIsoToXisoWithProgress rc=%d: %s", rc, msg);
  return env->NewStringUTF(msg);
}
