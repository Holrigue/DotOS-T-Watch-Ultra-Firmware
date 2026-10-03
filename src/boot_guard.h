#pragma once
// boot_guard.h - breaks a "dead home screen" boot loop.
//
// A watch whose battery ran flat once came back showing only the clock face and
// stayed that way across restarts and plain reflashes; only erasing the flash
// helped, so something saved in NVS was being replayed at every boot. The guard
// keeps a "boot in progress" counter in NVS: it is raised at the very start of
// setup() and cleared once the watch has run healthily for 30 s. A boot that
// finds the counter still raised knows the previous boot never finished, so it:
//   - runs in SAFE mode: skips the boot radios, the Notify restore and the
//     detector restore, which are the saved-state paths that touch the BLE stack;
//   - once that safe boot proves healthy, switches those saved choices off, so the
//     next boot cannot walk into the same wall (the user turns them back on);
//   - if three boots in a row never finish, wipes the saved settings (the same
//     remedy as flashing with "erase") and starts clean.
// Everything the guard does is printed on the serial console with a [bootguard]
// prefix, so the flasher's log shows which stage a boot stopped at.
#include <stdint.h>

// Call first thing in setup(), right after Serial.begin().
void boot_guard_begin();

// True for the whole boot when the previous boot did not finish.
bool boot_guard_safe_mode();

// Log the stage the boot has reached (serial only; keep calls cheap).
void boot_guard_stage(const char *stage);

// Call every loop(). Marks the boot healthy after kBootHealthyMs. Returns true
// exactly once, when a safe boot has just proven healthy and the risky saved
// choices were switched off - the caller then tells the user.
bool boot_guard_tick();
