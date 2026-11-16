#include <jni.h>

#include "arm64_interpreter.h"

extern "C" JNIEXPORT jint JNICALL
Java_org_astrans_emulator_MainActivity_nativeRunSelfTest(JNIEnv*, jobject) {
    return static_cast<jint>(astrans::run_self_test());
}
