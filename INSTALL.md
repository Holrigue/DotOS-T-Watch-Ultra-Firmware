# Installing DotOS

No toolchain, no command line. About 2 minutes.

## What you need

- A **LILYGO T-Watch Ultra**.
- A **USB-C data cable** (a charge-only cable will not work).
- A computer with **Google Chrome** or **Microsoft Edge** (Firefox and Safari cannot flash over USB).

## Flash it

1. On this page, click **`argus-dot-flasher.html`**, then the **download** button (top right of the file view).
2. Open the downloaded file in **Chrome or Edge** (double-click it).
3. **Hold the BOOT button on the watch while plugging it into USB-C.** This puts the watch in download mode; the browser cannot do it for you.
4. Click **Install** and pick the watch's serial port.
5. When asked about erasing:
   - **First install**, or coming from other firmware: **allow the erase**.
   - **Updating** a watch that already runs DotOS: **decline the erase** to keep your settings.
6. Wait for it to finish; the watch reboots on its own.

**If the port does not appear or it stalls:** hold **BOOT** while tapping **RESET**, then try again. Check that the cable carries data.

## Advanced: esptool

Flash the four binaries from this folder at these offsets, keeping the flash mode (`--flash_mode keep`):

| Offset | File |
|---|---|
| `0x0` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |

## Updating later

Download the newer `argus-dot-flasher.html` and repeat the steps, declining the erase.

## Optional extras

Wallpapers and hiking maps for the microSD card, the Android companion app and the source code are on the
[`develop` branch](../../tree/develop) and in the project's Releases. The watch works without them.
