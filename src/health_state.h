#pragma once
//
// health_state.h - device-side runtime around the pure health::HealthData model.
//
// Holds the one process-wide instance, caches it in NVS so a reboot shows the
// last known numbers (as stale) instead of blanks, drives the 2-minute
// heart-rate compile on the 1 Hz tick, and owns the fixed daily step goal set in
// Settings. The BLE transport (ans.cpp) hands raw packets to
// health_ingest_packet(); everything else reads through health_model().
//
// THREADING. BLE write callbacks run in the Bluetooth host task, not the Arduino
// loop. To keep the model single-threaded, health_ingest_packet() only copies the
// bytes into a small mailbox from the BLE task; the loop drains and applies them
// in health_tick_1hz(). So health_model() itself is only touched from the loop.
#include "health_data.h"
#include <cstddef>
#include <cstdint>

// The process-wide model. Read from the loop only (UI, accent bar, Santé screen).
health::HealthData &health_model();

// Load the cached snapshot + the stored step goal from NVS. Call once at boot,
// after Preferences is usable. Restored values read as stale until the relay
// refreshes them, but their last numbers are shown.
void health_boot_restore();

// 1 Hz from the main loop: drains any BLE packet mailbox into the model, runs the
// heart-rate compile, and periodically persists the snapshot when it changed.
void health_tick_1hz();

// Set + persist the fixed daily step goal (Settings). 0 disables the goal.
void health_set_step_goal(uint32_t goal);
uint32_t health_get_step_goal();

// True while the phone relay is actively feeding us: at least one of the fast
// metrics (heart rate, steps, stress) is present and inside its stale window.
// Drives the status-row heart icon (red when live, gray when not). Sleep is a
// once-a-day score with a 26 h window, so it is deliberately excluded - it would
// read "live" long after the relay stopped.
bool health_data_fresh();

// Called from the BLE task with a received health packet (see docs/health).
// Copies the bytes into the mailbox and returns; the loop applies them.
void health_ingest_packet(const uint8_t *data, size_t len);

// Monotonic count of health packets the loop has applied. The Health screen's
// swipe-up refresh samples it, then watches for it to change: an increment means
// the phone just pushed a fresh packet (refresh succeeded). Loop-only, like the
// model itself.
uint32_t health_rx_seq();

// millis() when the last packet was applied this session, or 0 if none has
// arrived since boot. The Health screen shows it as a "last sync" age. It is a
// session value (not persisted): a restored NVS snapshot reads as "no sync yet".
uint32_t health_last_rx_ms();

// Wire format for the health-input characteristic (see docs/health/README.md).
namespace health_packet {
constexpr uint8_t VERSION      = 1;
constexpr uint8_t BIT_SLEEP    = 1 << 0;
constexpr uint8_t BIT_STEPS    = 1 << 1;
constexpr uint8_t BIT_GOAL     = 1 << 2;
constexpr uint8_t BIT_STRESS   = 1 << 3;
constexpr uint8_t BIT_HR_AVG   = 1 << 4;
constexpr uint8_t BIT_HR_SAMPLE = 1 << 5;
constexpr uint8_t BIT_HR_MIN   = 1 << 6;   // lowest bpm over a recent window (u16)
constexpr uint8_t BIT_HR_MAX   = 1 << 7;   // highest bpm over a recent window (u16)
constexpr size_t  MAX_LEN      = 32;
}  // namespace health_packet
