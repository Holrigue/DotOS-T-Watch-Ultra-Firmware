# Installing DotOS

A step-by-step guide to get **DotOS** onto a **LILYGO T-Watch Ultra** — no
toolchain, no command line. Takes about 5 minutes.

## What you need

- A **LILYGO T-Watch Ultra** (ESP32-S3).
- A **USB-C data cable** (a charge-only cable will not work — it must carry data).
- A desktop computer running **Google Chrome** or **Microsoft Edge**.
  Firefox and Safari cannot flash over WebSerial.
- *(Optional)* a **microSD card** formatted **FAT32** for wallpapers and maps.
- *(Optional, Android only)* the **companion app** for health sync and phone
  notifications.

---

## Step 1 — Flash the firmware (the easy way)

1. Open the latest release:
   **https://github.com/Holrigue/ARGUS-DotOS/releases/latest**
2. Under **Assets**, download **`DotOS-<version>-flasher.html`**
   (a single self-contained file — everything is inside it).
3. **Open that file in Chrome or Edge** (double-click it).
4. **Hold the BOOT button on the watch while plugging it into USB-C.**
   This puts the watch in download mode. The browser cannot do this for you, so
   this step is required, not optional.
5. Click **Install / Flash** and pick the watch's serial port from the list.
6. When asked about erasing:
   - **First install**, or coming from other firmware → **allow the erase**.
   - **Updating** an existing DotOS watch → **decline the full erase** to keep
     your settings (watchface, Offense PIN, Notify choice). Your SD card is
     never touched either way.
7. Wait for it to finish, then let the watch reboot. Done.

> **Alternative — hosted web flasher:** you can instead open
> **https://holrigue.github.io/ARGUS-DotOS/** in Chrome/Edge and flash from
> there (same steps 4–7). It always serves the current release.

> **Alternative — esptool (advanced):** the release also ships the four raw
> binaries (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`).
> See the flashing section of the [technical reference](docs/REFERENCE.md#software--development)
> for the exact offsets. Keep `--flash_mode keep`.

**If the port does not appear or flashing stalls:** hold **BOOT** while tapping
**RESET**, then try again. Make sure you are using a *data* USB-C cable and
desktop Chrome/Edge.

---

## Step 2 — Add the SD-card assets (optional but recommended)

The mode wallpapers (and hiking map tiles) live on the microSD card so they can
change without reflashing.

1. Format a microSD card as **FAT32**.
2. From the same release, download **`dotos-<version>-sdcard.zip`**.
3. Unzip it and copy its **contents** to the **root** of the card.
4. Insert the card into the watch. Turn **Wallpaper** on in **Settings** to see
   them. (Without a card the watch simply shows no wallpaper — that is not an
   error.)

For offline hiking maps, see the [hiking guide](docs/hiking.md).

---

## Step 3 — The companion app (Android, optional)

The **ARGD-OS Dashboard** app relays phone **health data** and **notifications**
to the watch, and can push a **GPX route** (e.g. from Gaia GPS) to it.

1. Install the latest APK from
   **https://github.com/Holrigue/ARGD-OS-Dashboard-APK-/releases/latest**
   (download `DotOS-Dashboard-<version>.apk`, then side-load it — enable
   "install unknown apps" for your browser/file manager).
2. On first launch, grant **Nearby devices / Bluetooth** and, if you want health
   sync, the **Health Connect** permissions.

> iPhone needs no app for notifications — it uses Apple's built-in ANCS.

### Pairing

1. On the watch: open **Notify** (Tools) and **Enable** a mode
   (**Apple (ANCS)** for iPhone, **Android (Gadgetbridge)** for Android). This
   is what makes the watch advertise over Bluetooth.
2. **iPhone:** pair "Argus Watch" from iOS **Settings ▸ Bluetooth**.
   **Android:** in the companion app tap **Scan** and pick **ARGUS Watch**;
   for notifications, also install [Gadgetbridge](https://gadgetbridge.org/)
   (F-Droid) and add the device there.

The enabled Notify mode survives reboots, so you only do this once.

---

## Updating later

Grab the newer `DotOS-<version>-flasher.html` from the releases page and repeat
**Step 1**, declining the full erase to keep your settings. A full-erase flash
resets everything (you would re-enable Notify and re-pick your watchface).

---

Questions, bugs, or ideas → open an issue at
**https://github.com/Holrigue/ARGUS-DotOS/issues**.
