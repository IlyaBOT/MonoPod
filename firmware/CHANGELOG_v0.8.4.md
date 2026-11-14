# MonoPod v0.8.4 (research / candidate)

- Stable 16-hex hardware identity from ESP32 eFuse factory MAC; persistent NVS random fallback.
- USB CDC `MPOD1` protocol v2: INFO (SD totals/free/GUID), atomic-ish REPLACE with recovery journal, DELETE, NO_SPACE, no-card display warning.
- Studio: update existing same-named files over USB, delete from USB with confirmation; CLI `--replace` and `delete`.
- UI: long Select in Songs/Category -> delete confirmation for local `/Music` songs.
- Experimental, separate read-only-by-default microSD USB MSC lab for ESP32-S3 (SdFat v2), optionally enable writes on expendable media.
- Preliminary read-only classic `iTunesDB` inspector and technical research memo.
- Does NOT claim iTunes-compatible USB iPod or working M4A/ALAC playback.
