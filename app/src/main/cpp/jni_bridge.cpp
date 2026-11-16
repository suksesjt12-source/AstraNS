#include <jni.h>

#include <string>

#include "arm64_interpreter.h"
#include "vulkan_probe.h"

extern "C" JNIEXPORT jint JNICALL
Java_org_astrans_emulator_MainActivity_nativeRunSelfTest(JNIEnv*, jobject) {
    return static_cast<jint>(astrans::run_self_test());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_astrans_emulator_MainActivity_nativeVulkanInfo(JNIEnv* env, jobject) {
    const std::string info = astrans::vulkan_probe();
    return env->NewStringUTF(info.c_str());
}
