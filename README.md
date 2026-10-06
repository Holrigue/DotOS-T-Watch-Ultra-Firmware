# DotOS

**DotOS** is a [Nothing OS](https://nothing.tech/)–inspired firmware for the LILYGO T-Watch Ultra — an ESP32-S3 smartwatch (AMOLED, LoRa, GNSS, NFC, full sensor suite) reskinned around a monochrome-with-red **dot-matrix watchface**, a glanceable dashboard, and a phone **health bridge**, with the full ARGUS anti-surveillance / RF toolkit and a Meshtastic client one swipe away.

> **DotOS is a fork of [ARGUS](https://github.com/h4d35x0/argus), itself a fork of the phenomenal [`13:37` firmware by r3dfish](https://github.com/r3dfish/13-37).** The smartwatch core, the Meshtastic client, and the RF toolkit are their work; DotOS adds the Dot watchface, the companion health app, and a batch of daily-wear / notification / power refinements. Please star the originals — see [Credits](#credits).

> 🚀 **Install it in 2 minutes.** This branch (`main`) holds only what you need to put DotOS on your watch:
>
> | File | What it is |
> |---|---|
> | **[`argus-dot-flasher.html`](argus-dot-flasher.html)** | One-file web flasher: open it in Chrome or Edge. **The only file most people need.** |
> | [`INSTALL.md`](INSTALL.md) | The step-by-step guide. |
> | `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`, `FLASH.txt` | The raw binaries and their offsets, for advanced users who flash with `esptool`. |
>
> The source code, tests, SD-card assets and the [Technical Reference](https://github.com/Holrigue/ARGUS-DotOS/blob/develop/docs/REFERENCE.md) live on the [`develop` branch](https://github.com/Holrigue/ARGUS-DotOS/tree/develop).

## Apps

**Daily wear**
- **Dot watchface** — Nothing-OS dot-matrix clock with a red accent rail, compact date, live status glyphs, and two customizable data tiles.
- **Health** — sleep score, steps, and heart-rate high/low mirrored from your phone over BLE.
- **Notify** — mirrors phone notifications to your wrist (iPhone via ANCS, Android via Gadgetbridge).
- **Night time** — quiet hours you choose: no vibration and no screen wake for notifications (alarms still ring).
- **Music** — local MP3/FLAC player that reads straight off the SD card, sorted by artist folder.
- **Level** — digital bubble level (centre bullseye plus horizontal and vertical vials) using the accelerometer.
- **Compass** — relative gyro heading with a Set-North pin (this board has no magnetometer).
- **GPS** — GNSS fix, satellite count, coordinates, and automatic timezone.
- **Alarm · Stopwatch · Timer · Calendar** — the timepiece hub, one swipe up from the clock.
- **Map / GPX Track** — follow a GPX route live on the watch (optional SD map tiles); routes can be pushed from the phone (e.g. shared from Gaia GPS).

**Mesh & radio**
- **Meshtastic** — LoRa mesh client: messages, nodes, DMs, traceroute, position requests, and a slippy map.
- **NFC** — read and write ISO 14443 / 15693 tags.
- **TPMS · Pager · APRS** — receive tire-pressure sensors, POCSAG/FLEX pages, and LoRa-APRS frames.
- **WiFi · Port Scanner** — survey and join networks, ping-sweep the LAN, and scan a host's ports.
- **Analyze** — WiFi / BLE / LoRa spectrum analyzers.
- **Wardriver** — logs WiFi APs and BLE devices with GPS to WiGLE-style CSV.
- **Mouse · USB SD** — use the watch as a BLE trackpad, or mount its SD card to a computer over USB-C.

**Anti-surveillance**
- **Threat Radar** — correlates tracker / AP sightings against your GPS movement to flag whether a device may be *following you*, with haptic and on-face alerts.
- **Detectors** — separated AirTag / Find My trackers, Flipper Zero, card skimmers, evil-twin APs, surveillance vendors (Flock / Axon / Ring), and a nearby-phone Human Detector.
- **Presence radar** — a live radar/sonar-style screen that visualises the people and devices detected around you right now.
- **HexHound** — a gamified recon "pet" that grows with the RF activity around you.

### Offense mode (optional)

A PIN-gated **Offense** mode groups the RF-*transmit* tools. It is **off by default, not recommended, and provided only for authorized security testing on hardware you own** — enabled solely at the user's explicit consent. Details are intentionally kept out of this README.

## What ARGUS keeps, and for how long

ARGUS is an anti-surveillance tool, so it owes you a straight answer about its
own data. Every detector writes a record to the SD card when it first alerts on
a contact: `/AirTag`, `/Flipper`, `/Skimmers`, `/EvilTwin` and `/ThreatRadar`
each keep a `discovered.txt`, and `/Flock` keeps one file per hit. A record
holds a timestamp, the device's MAC, signal details, and your position
**rounded to roughly 110 m** (not the full-precision fix, which is used for
detection but never written).

**Those records expire.** Retention is capped at **30 days** and **300 entries
per log**, oldest evicted first. The window is enforced every time a record is
appended and swept once at boot, so logs also age out on a watch that has not
detected anything in a month. **Settings** shows the live record count and
offers a one-tap **Clear detection logs**.

The MAC is stored in the clear, deliberately. This log is *your* evidence about
a device that followed *you*, and a hashed MAC cannot be matched against a
device you later physically identify. What is bounded is how long it is kept,
not how useful it is. The policy and the reasoning live in
[src/detect/log_retention.h](https://github.com/Holrigue/ARGUS-DotOS/blob/develop/src/detect/log_retention.h) and are covered by the
host test suite.

Nothing leaves the watch on its own. The only outbound transmission tied to
detection is a 32-bit hashed tracker fingerprint on the Meshtastic mesh when a
tail reaches Confirmed, and only when the mesh is up. Position is never
broadcast unless you turn on **Broadcast Location**, which defaults to off.

## Companion app

[**ARGD-OS Dashboard**](https://github.com/Holrigue/ARGD-OS-Dashboard-APK-) (Android, Kotlin / Compose) reads sleep / steps / heart-rate from **Health Connect** and pushes them to the watch, relays phone notifications, and can push a GPX route to the watch — over a direct BLE link, no cloud. iOS needs no app for notifications (ANCS); the health bridge is Android-only for now.

## Hardware

LILYGO T-Watch Ultra: an **ESP32-S3** with a **2.06″ AMOLED**, **LoRa (SX1262)**, **GNSS**, **NFC**, a **6-axis IMU** (accel + gyro, no magnetometer), and a **1100 mAh** battery. It can be bought from [LilyGo](https://www.lilygo.cc/cpstlm) (the US version is the 915 MHz model).

**Screen — pixels and corners to consider.** The firmware renders to a full **410 × 502** framebuffer, but the round watch case masks it to a **~410 px circle**: the centre (about a 290 × 290 px square) is always safe, while the **top and bottom strips (~46 px each)** and the **corners** of any full-width element sit behind the bezel. Only ~64 % of the framebuffer is actually visible, so keep essential UI inside the disc.

Full pinouts, power channels, the component list, radio bands, power-consumption figures, and the exact visible-area geometry are in the [Technical Reference](https://github.com/Holrigue/ARGUS-DotOS/blob/develop/docs/REFERENCE.md#display).

## Build & flash

Built with **PlatformIO** (on the [`develop` branch](https://github.com/Holrigue/ARGUS-DotOS/tree/develop)):

```bash
pio run              # build
pio run -t upload    # flash over USB-C (hold BOOT while connecting for download mode)
```

Prebuilt, flashable binaries **and a one-click web flasher** ship with every [Release](https://github.com/Holrigue/ARGUS-DotOS/releases). There is deliberately **no single merged image** (a merged image bricked a watch once by rewriting the bootloader's flash-mode byte). The full build notes, prebuilt-flashing steps (esptool offsets, `--flash_mode keep`), SD-card assets, crash forensics, and library patches are in the [Technical Reference](https://github.com/Holrigue/ARGUS-DotOS/blob/develop/docs/REFERENCE.md#software--development).

## Credits

DotOS is a fork of **[ARGUS](https://github.com/h4d35x0/argus)**, itself a fork of **[`13:37`](https://github.com/r3dfish/13-37)** by **[r3dfish](https://github.com/r3dfish)**. The smartwatch core, the Meshtastic client, and the RF toolkit are theirs — DotOS layers on the Dot watchface, the companion health app, and the daily-wear / notification / power refinements. **The core is theirs.** If this project is useful to you, please go support the OGs!

Additional inspiration and thanks to **[sacriphanius](https://gitlab.com/sacriphanius)** and the **[SCR-Terminal](https://gitlab.com/sacriphanius/scr-terminal)** project, and to **[peter-neu](https://github.com/peter-neu)** and the **[OutdoorWatch](https://github.com/peter-neu/t-watch_ultra)** project.

Star them, build their projects, **THANK THEM.**

Thanks also to LILYGO for the hardware, the [Meshtastic](https://meshtastic.org/) project, and the maintainers of LVGL, RadioLib, LilyGoLib, and the other libraries this firmware stands on.



## License

MIT (see [LICENSE](LICENSE)) for everything under `src/` (on `develop`), `scripts/`, and the project configuration. DotOS retains the upstream projects' licenses and copyright where their code is used; the full third-party license list is in the [Technical Reference](https://github.com/Holrigue/ARGUS-DotOS/blob/develop/docs/REFERENCE.md#third-party-licenses).

### Responsible use

This firmware includes RF-transmit and wireless-monitoring tools. Transmitting on regulated bands and capturing wireless traffic are subject to local law and licensing. Use it only on equipment and networks you own or are explicitly authorized to test, and for educational or defensive purposes. The anti-stalking features are an aid, not a safety guarantee: do not rely on ARGUS alone if you believe you are being followed — contact local authorities. Provided "as is", without warranty.
