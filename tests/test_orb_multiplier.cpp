#include <cassert>
#include <cmath>
#include <limits>
#include "kzsf_orbs.h"
#include "../src/orb_hook.h"
#include <vector>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "kzsf_settings.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

extern "C" float __attribute__((sysv_abi)) kzsf_orbs_gain(void*, float);
extern "C" void kzsf_orbs_status(int);
extern "C" void kzsf_cheat_status(unsigned);
static void __attribute__((sysv_abi)) Setter(void* player, float balance) {
    balance = std::min(balance, 999999.0f);
    std::memcpy(static_cast<unsigned char*>(player) + 0x1e8, &balance, 4);
}

int main(int argc, char** argv) {
    assert(KzsfOrbs::Scale(100, 110, 2) == 120);
    assert(KzsfOrbs::Scale(100, 90, 100) == 90);
    assert(KzsfOrbs::Scale(100, 100, 100) == 100);
    assert(KzsfOrbs::Scale(100, 110, 1) == 110);
    assert(KzsfOrbs::Scale(0, 1, 100) == 100);
    float balance = 0;
    for (int i = 0; i < 10; ++i) balance = KzsfOrbs::Scale(balance, balance + 1, 0.1f);
    assert(std::abs(balance - 1) < 0.00001f);
    assert(KzsfOrbs::Scale(balance, balance + 2, 2) == balance + 4);
    assert(KzsfOrbs::Scale(100, 110, std::numeric_limits<float>::quiet_NaN()) == 110);
    assert(KzsfOrbs::ClampMultiplier(0) == 0.1f);
    assert(KzsfOrbs::ClampMultiplier(101) == 100);
    assert(KzsfOrbs::ClampMultiplier(std::numeric_limits<float>::infinity()) == 1);
    std::vector<unsigned char> image(0x6fbdc);
    uint64_t calls[3];
    assert(kzsf_orb_find(image.data(), image.size(), calls) == UINT64_MAX);
    assert(kzsf_orb_find(image.data(), 1, calls) == UINT64_MAX);
    if (argc > 1) {
        FILE* file = std::fopen(argv[1], "rb");
        assert(file && !std::fseek(file, 0x4000, SEEK_SET));
        assert(std::fread(image.data(), 1, image.size(), file) == image.size());
        std::fclose(file);
        assert(kzsf_orb_find(image.data(), image.size(), calls) == 0x6a9f0);
        image[calls[1] + 1] ^= 1;
        assert(kzsf_orb_find(image.data(), image.size(), calls) == UINT64_MAX);
    }
#ifdef _WIN32
    auto* stub = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    auto* stub = static_cast<unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(stub != MAP_FAILED);
#endif
    assert(stub);
    kzsf_orb_build_stub(stub, reinterpret_cast<uintptr_t>(kzsf_orbs_gain), reinterpret_cast<uintptr_t>(Setter));
#ifdef _WIN32
    DWORD previous;
    assert(VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &previous));
    FlushInstructionCache(GetCurrentProcess(), stub, 26);
#else
    assert(!mprotect(stub, 4096, PROT_READ | PROT_EXEC));
#endif
    using Hook = void (__attribute__((sysv_abi)) *)(void*, float);
    auto hook = reinterpret_cast<Hook>(stub);
    std::array<unsigned char, 0x200> player{};
    balance = 100;
    std::memcpy(player.data() + 0x1e8, &balance, 4);
    auto& settings = KzsfSettings::Get();
    settings.red_orb_multiplier = 2;
    hook(player.data(), 110);
    std::memcpy(&balance, player.data() + 0x1e8, 4);
    assert(balance == 120);
    settings.cheats[4] = true;
    assert(kzsf_orbs_gain(player.data(), 130) == 140);
    kzsf_cheat_status(4);
    assert(kzsf_orbs_gain(player.data(), 130) == 130);
    settings.cheats[4] = false;
    assert(kzsf_orbs_gain(player.data(), 130) == 140);
    settings.red_orb_multiplier = 100;
    hook(player.data(), 90);
    std::memcpy(&balance, player.data() + 0x1e8, 4);
    assert(balance == 90);
    hook(player.data(), 100000);
    std::memcpy(&balance, player.data() + 0x1e8, 4);
    assert(balance == 999999);
    kzsf_orbs_status(1);
    assert(settings.red_orbs_supported);
    kzsf_orbs_status(0);
    assert(!settings.red_orbs_supported);
    auto config = std::filesystem::temp_directory_path() / "kzsf-orb-multiplier-test.ini";
    assert(!std::filesystem::exists(config));
#ifdef _WIN32
    _putenv_s("KZSF_CONFIG", config.string().c_str());
#else
    setenv("KZSF_CONFIG", config.string().c_str(), 1);
#endif
    settings.red_orb_multiplier = 0.125f;
    settings.cheats[0] = true;
    settings.cheats[2] = true;
    KzsfSettings::Save();
    settings.red_orb_multiplier = 1;
    settings.cheats[0] = false;
    settings.cheats[2] = false;
    KzsfSettings::Load();
    assert(settings.red_orb_multiplier == 0.125f);
    assert(settings.cheats[0] && settings.cheats[2] && !settings.cheats[1] && !settings.cheats[4]);
    assert(std::filesystem::remove(config));
#ifdef _WIN32
    VirtualFree(stub, 0, MEM_RELEASE);
#else
    munmap(stub, 4096);
#endif
    std::puts("orb-multiplier-test: OK (gains, spending, fractions, signatures, executable hook)");
}
