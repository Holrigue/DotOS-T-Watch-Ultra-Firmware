#pragma once
// boot_guard_logic.h - the pure decision behind the boot guard (src/boot_guard.cpp).
// Header-only and hardware-free so it is covered by the host tests
// (test/test_boot_guard.cpp).
//
// The guard counts boots that did NOT get far enough to be called healthy (the
// watch hung, or reset, before it had run for kBootHealthyMs). Reading that
// count at the start of a boot says how much the previous boots can be trusted.
#include <cstdint>

enum class BootGuardLevel : uint8_t {
    Normal,   // last boot finished: restore everything as usual
    Safe,     // last boot did not finish: skip the risky restores (radios, Notify, detectors)
    Reset,    // several boots in a row did not finish, even in Safe: wipe the saved settings
};

constexpr uint32_t kBootHealthyMs   = 30000;  // running this long = the boot counts as finished
constexpr uint8_t  kBootResetAfter  = 3;      // unfinished boots in a row that trigger the wipe

inline BootGuardLevel boot_guard_level(uint8_t unfinished)
{
    if (unfinished == 0) return BootGuardLevel::Normal;
    if (unfinished < kBootResetAfter) return BootGuardLevel::Safe;
    return BootGuardLevel::Reset;
}

// The count to store while THIS boot is in progress (saturates, so a long run of
// bad boots can never wrap back to "fine"). A Reset boot starts the count over.
inline uint8_t boot_guard_next_count(uint8_t unfinished)
{
    if (unfinished >= kBootResetAfter) return 1;
    return (uint8_t)(unfinished + 1);
}
