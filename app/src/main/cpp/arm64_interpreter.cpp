#include <array>
#include <cstdint>
#include <jni.h>

namespace astrans {
struct CpuState {
    std::array<uint64_t, 31> x{};
    uint64_t pc = 0;
};

bool execute_one(CpuState& cpu, uint32_t instruction) {
    if (instruction == 0xD503201F) { // NOP
        cpu.pc += 4;
        return true;
    }
    if ((instruction & 0xFFE00000U) == 0xD2800000U) { // MOVZ Xd, #imm16
        const unsigned rd = instruction & 0x1FU;
        const uint64_t imm16 = (instruction >> 5U) & 0xFFFFU;
        if (rd == 31) return false;
        cpu.x[rd] = imm16;
        cpu.pc += 4;
        return true;
    }
    if ((instruction & 0xFF000000U) == 0x91000000U) { // ADD Xd, Xn, #imm12
        const unsigned rd = instruction & 0x1FU;
        const unsigned rn = (instruction >> 5U) & 0x1FU;
        const uint64_t imm12 = (instruction >> 10U) & 0xFFFU;
        if (rd == 31 || rn == 31) return false;
        cpu.x[rd] = cpu.x[rn] + imm12;
        cpu.pc += 4;
        return true;
    }
    if (instruction == 0xD65F03C0) return true; // RET X30
    return false;
}

int run_self_test() {
    constexpr std::array<uint32_t, 3> program{
        0xD28000E0U, // MOVZ X0, #7
        0x91001400U, // ADD X0, X0, #5
        0xD65F03C0U  // RET
    };
    CpuState cpu{};
    for (const uint32_t instruction : program) {
        if (!execute_one(cpu, instruction)) return -1;
        if (instruction == 0xD65F03C0U) break;
    }
    return static_cast<int>(cpu.x[0]);
}
} // namespace astrans

extern "C" JNIEXPORT jint JNICALL
Java_org_astrans_emulator_MainActivity_nativeRunSelfTest(JNIEnv*, jobject) {
    return static_cast<jint>(astrans::run_self_test());
}
