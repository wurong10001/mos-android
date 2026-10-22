#include "java_ui.h"

#include <android_native_app_glue.h>
#include <jni.h>

#include "log.h"

namespace {

const char* kPackageName = "org.wurong.mos";
const char* kErrorActivity = "org.wurong.mos.ErrorActivity";

const char* kExtraErrorKey = "org.wurong.mos.ERROR_KEY";
const char* kExtraDetails = "org.wurong.mos.DETAILS";
const char* kExtraLogPath = "org.wurong.mos.LOG_PATH";
const char* kExtraCrashPath = "org.wurong.mos.CRASH_PATH";

constexpr jint kFlagActivityNewTask = 0x10000000;

void putStringExtra(JNIEnv* env, jobject intent, jmethodID putExtra,
                    const char* key, const char* value)
{
    if (value == nullptr) return;
    jstring k = env->NewStringUTF(key);
    jstring v = env->NewStringUTF(value);
    if (k != nullptr && v != nullptr) {
        env->CallObjectMethod(intent, putExtra, k, v);
    }
    env->DeleteLocalRef(k);
    env->DeleteLocalRef(v);
}

}  // namespace

bool ui_show_error(struct android_app* app, const char* errorKey,
                   const char* details, const char* logPath,
                   const char* crashPath)
{
    if (app == nullptr || app->activity == nullptr) {
        LOGE("ui_show_error: no activity");
        return false;
    }
    JavaVM* vm = app->activity->vm;
    if (vm == nullptr) {
        LOGE("ui_show_error: no JavaVM");
        return false;
    }

    bool attached = false;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("ui_show_error: AttachCurrentThread failed");
            return false;
        }
        attached = true;
    }
    if (env == nullptr) {
        if (attached) vm->DetachCurrentThread();
        return false;
    }

    bool ok = false;
    jstring pkgName = env->NewStringUTF(kPackageName);
    jstring clsName = env->NewStringUTF(kErrorActivity);
    jclass intentClass = env->FindClass("android/content/Intent");
    jobject intent = nullptr;

    if (intentClass != nullptr && pkgName != nullptr && clsName != nullptr) {
        jmethodID ctor = env->GetMethodID(intentClass, "<init>", "()V");
        if (ctor != nullptr) {
            intent = env->NewObject(intentClass, ctor);
        }
    }

    if (intent != nullptr) {
        jmethodID setClassName = env->GetMethodID(
            intentClass, "setClassName",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        if (setClassName != nullptr) {
            env->CallObjectMethod(intent, setClassName, pkgName, clsName);
        }

        jmethodID putExtra = env->GetMethodID(
            intentClass, "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        if (putExtra != nullptr) {
            putStringExtra(env, intent, putExtra, kExtraErrorKey, errorKey);
            putStringExtra(env, intent, putExtra, kExtraDetails, details);
            putStringExtra(env, intent, putExtra, kExtraLogPath, logPath);
            putStringExtra(env, intent, putExtra, kExtraCrashPath, crashPath);
        }

        jmethodID addFlags = env->GetMethodID(intentClass, "addFlags",
                                              "(I)Landroid/content/Intent;");
        if (addFlags != nullptr) {
            env->CallObjectMethod(intent, addFlags, kFlagActivityNewTask);
        }

        jclass activityClass = env->GetObjectClass(app->activity->clazz);
        jmethodID startActivity = env->GetMethodID(
            activityClass, "startActivity", "(Landroid/content/Intent;)V");
        if (startActivity != nullptr) {
            env->CallVoidMethod(app->activity->clazz, startActivity, intent);
            ok = true;
        } else {
            LOGE("ui_show_error: startActivity method not found");
        }
        env->DeleteLocalRef(activityClass);
    } else {
        LOGE("ui_show_error: Intent creation failed");
    }

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ok = false;
    }

    // DeleteLocalRef 允许传 nullptr
    env->DeleteLocalRef(intent);
    env->DeleteLocalRef(intentClass);
    env->DeleteLocalRef(pkgName);
    env->DeleteLocalRef(clsName);

    if (attached) {
        vm->DetachCurrentThread();
    }

    LOGI("ui_show_error(%s): %s", errorKey ? errorKey : "?", ok ? "started" : "failed");
    return ok;
}
