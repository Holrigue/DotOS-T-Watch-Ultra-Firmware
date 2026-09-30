# DotOS — planned features (owner's shortlist)

Captured from Gabriel's final feature shortlist. Not yet built; listed here so the
plan survives between sessions. Order is not priority.

## A. LocalSend receive
Bring [LocalSend](https://github.com/localsend/localsend)-style local file transfer
to the watch — over the same LAN as phones/PCs. On the watch, **receive-only** is
enough (accept files pushed from a phone/PC running LocalSend, land them on the
SD card). LocalSend is an open protocol (HTTP + multicast discovery over Wi-Fi);
the watch would advertise/answer discovery and accept an incoming transfer.

## B. OTA firmware update
An on-watch updater that fetches the latest **official GitHub Release** of the
watch firmware and flashes it over the air (Wi-Fi pull from the Releases API →
verify → write to the OTA partition → reboot). Ties into the repo's existing
`release.yml` assets (bootloader/partitions/boot_app0/firmware bins).

## C. Remote media & input control
A "Remote" app that acts over BLE HID to a paired PC/phone:
- Media row: **Back · Play/Pause · Next**.
- Below: a **volume** control bar.
- Below that: a large empty pad labelled **"remote mouse"** (trackpad → moves the
  wireless cursor).
- A full-width **keyboard** button (same width as the menu buttons) that
  slides up a simplified full-screen on-watch keyboard for **remote typing**,
  dismissable with a **▼** icon.
- Reference for the UX (to be simplified for the watch):
  [BLEK](https://play.google.com/store/apps/details?id=io.appground.blek).

## D. Wi-Fi "Rickroll" (Offense)
Add a **Wi-Fi Rickroll** entry to the Offense apps (beacon-spam SSIDs spelling the
lyrics), alongside the existing WiFi offense tools.

## E. Reticulum / Sideband link (big one — real use case)
Let the watch join the owner's [Reticulum](https://reticulum.network) network so it
can send/receive messages with friends running
[Sideband](https://unsigned.io/sideband/) on Android — **offline comms on a hike**.
The watch already has 915 MHz LoRa (Meshtastic) hardware, matching the owner's own
Reticulum RF network. Two transports wanted:
- **TCP/Web**: link to the owner's Reticulum TCP/web server when on a network.
- **Radio**: bridge to the owner's LoRa/antenna RF Reticulum network directly.
Goal: use the watch as a Reticulum node for text comms off-grid.

---
_Delivered so far (context): Dot face rework, Facewatch fonts, HexHound mascot
fix, hiking GPX follow (Map/GPX Track/REC + PC tile tool), Level app, phone
notification relay + tofu fix, and Gaia-route "share to watch" over BLE._
