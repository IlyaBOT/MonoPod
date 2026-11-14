# v0.8.5 — exclusive USB/Wi-Fi Sync mode

- Introduced `SyncMode.h/.cpp` shared transport/SD ownership gate (Idle, USB CDC, Wi-Fi; USB MSC reserved for future iTunes work).
- Starting either mode now stops I2S/decoder playback immediately, silences piezo click effects and locks normal UI and track controls.
- New 160×128 fullscreen Sync UI with USB/Wi-Fi pictogram, large SYNC text and permanent no-SD-removal/cable warning.
- USB CDC requires explicit `MPOD1 SYNC START` before PUT/REPLACE/DELETE/SCAN; `HELLO`/`INFO` never engage Sync. Exit via `MPOD1 SYNC END` only when no transfer is in progress; library rescanned after exit.
- Python USB client/Studio automatically brackets mutating transfers in sync sessions; status/port detection does not start Sync.
- Wi-Fi Sync starts only from Settings; it stops audio before the AP is enabled. Normal input is locked; hold Select 1.5s when idle to exit. Server refuses to stop during active file/theme uploads.
- Wi-Fi audio uploads now stage the file and replace the existing same-name track with recovery journal/backup instead of rejecting duplicates. Unsupported M4A/ALAC remains unsupported.
- Maintained Wokwi mirrored code for updated UI and controllers.
- Added long-form Codex reverse-engineering task for *original iPod 3G (A1040)* USB protocol, USB Mass Storage/iTunesDB and iTunes compatibility. No proprietary Apple firmware included.
- Python regression suite: 33 tests passed. Arduino full build and device tests NOT performed.
