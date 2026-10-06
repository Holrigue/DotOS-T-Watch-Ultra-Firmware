# DotOS — install

**DotOS** is a [Nothing OS](https://nothing.tech/)–inspired firmware for the **LILYGO T-Watch Ultra** (ESP32-S3).
It is a fork of [ARGUS](https://github.com/h4d35x0/argus), itself a fork of the
[`13:37` firmware by r3dfish](https://github.com/r3dfish/13-37) — please star the originals.

This branch holds **only what you need to put DotOS on your watch**:

| File | What it is |
|---|---|
| **`argus-dot-flasher.html`** | One-file web flasher. Everything is inside it. **This is the only file most people need.** |
| `INSTALL.md` | The step-by-step guide (2 minutes). |
| `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`, `FLASH.txt` | The raw binaries and their offsets, for advanced users who flash with `esptool`. |
| `LICENSE`, `licenses/` | Licences. |

**Quick start:** download `argus-dot-flasher.html`, open it in Chrome or Edge, hold **BOOT** on the
watch while plugging it in, click **Install**. Full steps in [INSTALL.md](INSTALL.md).

Source code, tests, SD-card assets and the technical reference live on the
[`develop` branch](../../tree/develop).
