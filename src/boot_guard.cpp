// boot_guard.cpp - see boot_guard.h.
#include "boot_guard.h"
#include "boot_guard_logic.h"

#include <Arduino.h>
#include <Preferences.h>
#include "esp_system.h"
#include "nvs_flash.h"

namespace {

const char *const NS      = "argusboot";
const char *const KEY_CNT = "pend";

BootGuardLevel s_level   = BootGuardLevel::Normal;
bool           s_healthy = false;

const char *level_name(BootGuardLevel l)
{
    switch (l) {
        case BootGuardLevel::Normal: return "normal";
        case BootGuardLevel::Safe:   return "SAFE";
        default:                     return "RESET";
    }
}

uint8_t read_count()
{
    Preferences p;
    if (!p.begin(NS, true)) return 0;
    uint8_t n = p.getUChar(KEY_CNT, 0);
    p.end();
    return n;
}

void write_count(uint8_t n)
{
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putUChar(KEY_CNT, n);
    p.end();
}

// Switch off the saved choices that were skipped on the safe boot, so the next
// boot does not replay them. Only the on/off flags change: the phone platform,
// the pairing and every other setting stay as they were.
void disable_risky_restores()
{
    Preferences n;
    if (n.begin("argusnotify", false)) { n.putBool("en", false); n.end(); }
    Preferences d;
    if (d.begin("argusdet", false)) { d.clear(); d.end(); }
}

}  // namespace

void boot_guard_begin()
{
    const uint8_t unfinished = read_count();
    s_level = boot_guard_level(unfinished);
    s_healthy = false;

    Serial.printf("[bootguard] reset_reason=%d unfinished=%u -> %s\n",
                  (int)esp_reset_reason(), (unsigned)unfinished, level_name(s_level));

    if (s_level == BootGuardLevel::Reset) {
        // Same remedy as flashing with "erase": drop every saved setting. Nothing
        // else has read NVS yet this early in setup(), so it is safe to do here.
        Serial.println("[bootguard] wiping NVS (3 unfinished boots in a row)");
        nvs_flash_deinit();
        nvs_flash_erase();
        nvs_flash_init();
    }
    write_count(boot_guard_next_count(unfinished));
}

bool boot_guard_safe_mode() { return s_level == BootGuardLevel::Safe; }

void boot_guard_stage(const char *stage)
{
    Serial.printf("[bootguard] t=%lu stage=%s\n", (unsigned long)millis(), stage);
}

bool boot_guard_tick()
{
    if (s_healthy || millis() < kBootHealthyMs) return false;
    s_healthy = true;
    write_count(0);
    boot_guard_stage("healthy");
    if (s_level != BootGuardLevel::Safe) return false;
    disable_risky_restores();
    return true;
}
