#include <stdio.h>
#include <string.h>

#include <SDL_system.h>
#include <jni.h>

static void xemu_android_show_toast(const char *msg, int duration)
{
    if (!msg || !msg[0]) {
        return;
    }

    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) {
        return;
    }

    jclass activity_class = (*env)->GetObjectClass(env, activity);
    if (!activity_class) {
        return;
    }

    jmethodID show_toast = (*env)->GetStaticMethodID(
        env, activity_class, "showToast", "(Ljava/lang/String;IIII)I");
    if (!show_toast) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, activity_class);
        return;
    }

    jstring message = (*env)->NewStringUTF(env, msg);
    if (!message) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, activity_class);
        return;
    }

    (*env)->CallStaticIntMethod(env, activity_class, show_toast,
                                message, duration, -1, 0, 0);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, message);
    (*env)->DeleteLocalRef(env, activity_class);
}

static int xemu_android_should_suppress_toast(const char *msg)
{
    if (!msg) {
        return 0;
    }

    return strncmp(msg, "Connected '", 11) == 0 &&
           strstr(msg, "' to port ") != NULL;
}

void xemu_queue_notification(const char *msg)
{
    if (msg) {
        fprintf(stderr, "[xemu] %s\n", msg);
        if (!xemu_android_should_suppress_toast(msg)) {
            xemu_android_show_toast(msg, 0);
        }
    }
}

void xemu_queue_error_message(const char *msg)
{
    if (msg) {
        fprintf(stderr, "[xemu][error] %s\n", msg);
    }
}
