#pragma once
//
// power_mgmt.h - one-time PMU (AXP2101) power/charge policy + the battery
// longevity setting.
//
// The firmware otherwise inherits LilyGoLib's charge defaults. Here we set a few
// conservative, well-documented AXP2101 policies that make charging behave across
// different sources and protect the cell across a full 100 -> 0% cycle:
//
//   - VBUS input VOLTAGE limit (VINDPM): if the source sags under load (a weak
//     USB port, a thin/long cable), the PMU automatically backs off the input
//     current instead of browning out. This is what lets "any charger, any
//     wattage" behave gracefully without USB-PD negotiation.
//   - VBUS input CURRENT ceiling: a sane cap for this small cell + system rail.
//   - System power-down voltage: cut off before the cell over-discharges at the
//     0% end (deep-discharge protection - longevity and safety).
//   - Charge target voltage: full (4.2 V) for maximum runtime, or long-life
//     (4.1 V) for roughly double the cycle life at ~10% less capacity.
//     User-selectable and persisted.
#include <cstdint>

// Apply the fixed policies + the saved longevity choice. Call once at boot,
// after instance.begin() and once Preferences is usable.
void power_boot_config();

// Battery longevity: false = charge to 4.2 V (full, the default), true = 4.1 V
// (gentler on the cell). Applies immediately and persists in NVS.
void power_set_longevity(bool on);
bool power_get_longevity();
