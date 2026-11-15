# MonoPod MPOD1 v4 and HTTP API

All numbers below are unsigned decimal unless marked hex. All operations are sequential. Connecting/HELLO/INFO do not enter Sync. `SYNC START` is an explicit user operation; it stops audio and acquires SD exclusively. Wi-Fi HTTP exists only while the user has started Wi-Fi Sync on the player. LAN STA is reused, ongoing reconnect is allowed to complete, and AP is fallback only.

## USB

Text commands and replies use UTF-8 lines: `MPOD1 <payload>\n`. Native USB CDC only; UART0 remains the developer console. Never send commands while a NEXT binary request is outstanding. HELLO replies `MPOD1 READY 4 SD=1`. Versions 1–3 retain a capability-limited client path. INFO retains `OK INFO SD=… TRACKS=… TOTAL_BYTES=… FREE_BYTES=… GUID=…` and adds FW/PROTOCOL. Legacy HELLO, INFO, SYNC START/END, PUT, REPLACE, DELETE and SCAN remain supported with their /Music target model.

Version 4 extensions use `MPOD1 X <command>`. Paths are uppercase/lowercase hexadecimal UTF-8 bytes without delimiters, e.g. `/Music` = `2F4D75736963`. This prevents spaces and Unicode names from changing token boundaries.

| X command | Arguments | Response |
|---|---|---|
| INFO, STORAGE | none | OK followed by DeviceInfo JSON |
| LIST | pathHex offset limit | OK {entries:[…],next:n}; limit 1–32, next=-1 means end |
| STAT, META | pathHex | OK {name,path,directory,size,modified}; META provides range-read provenance, not fabricated index tags |
| READ, GET | pathHex offset count | DATA decimalLength 8hexCRC followed immediately by exactly that many raw bytes; count 0–1024 |
| MKDIR | pathHex | OK DONE |
| DELETE | filePathHex | OK DONE |
| RMDIR | directoryPathHex | OK DONE; empty directories only |
| RENAME | sourcePathHex destinationPathHex | OK DONE; existing destination rejected |
| PUT | pathHex size 8hexCRC replace(0/1) | NEXT blockSize, raw block, NEXT…; final OK SAVED |
| THEME | pathHex size 8hexCRC 1 | Same transaction as PUT; target must be /MonoPod/theme.mpt |
| THEMEINFO | optional path ignored | STAT of installed theme |
| SMOOTH | 0/1 | OK SMOOTHING |

The next READ command acknowledges successful receipt of the previous block. There is no unsolicited binary download stream. CRC32 is standard IEEE reflected polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF. Zero-length READ has CRC 00000000. An empty PUT commits without NEXT. File size is bounded to 1 GiB, but large audio data never resides in device RAM. Upload NEXT is limited to 1024 bytes; each requested block is written before the next request. Bad CRC/length never replaces the old file. After an upload error with uncertain framing the host closes the port. Device receiver aborts after 15 seconds idle, drains residual input until 1.5 seconds quiet; reconnect after 18 seconds and restart the transfer.

DeviceInfo JSON contains firmware, protocol, sd_ready, total_bytes, free_bytes (64-bit byte quantities), tracks, sync_mode, ip, audio_formats and capabilities. Supported capabilities include list/stat/read/fs_upload/delete/mkdir/rename/theme/theme_get/scan/metadata_ranges/smoothing/sync_end. Unknown firmware is not assumed to support extensions.

Errors are `MPOD1 ERR <CODE>`. Examples: NO_SD, SYNC_REQUIRED, SYNC_BUSY, INVALID_PATH, PROTECTED_PATH, PROTECTED_ROOT, NOT_FOUND, DIRECTORY_NOT_EMPTY, ALREADY_EXISTS, NO_SPACE, SD_WRITE, SIZE_OR_CRC_MISMATCH, INVALID_THEME_OR_CRC, RECOVERY_PENDING. No success is inferred from a byte count alone.

## HTTP

Paths use URL query encoding. HTTP is unauthenticated local HTTP; do not publish/forward port 80 onto the Internet. One Desktop connection owns a transfer queue; concurrent independent clients are not a supported workflow. No access-token scheme was introduced.

| Method | Route | Arguments |
|---|---|---|
| GET | /api/info | DeviceInfo |
| GET | /api/storage | byte storage information in DeviceInfo |
| GET | /api/fs/list | path, offset=0, limit=32 |
| GET | /api/fs/stat, /api/fs/metadata | path |
| GET | /api/fs/download | path, offset, count (0–16384); raw body and X-CRC32 header |
| POST | /api/fs/upload | multipart file, query path, size, crc(8hex), replace(0/1); ordinary files only |
| POST | /api/theme | multipart theme, query size, crc(8hex), replace=1; dedicated /MonoPod/theme.mpt target |
| POST | /api/fs/mkdir | path |
| POST | /api/fs/rename | path, destination |
| DELETE | /api/fs/file | path, directory(0/1) |
| GET | /api/theme/info | installed theme stat |
| POST | /api/scan | rebuild media index |
| POST | /api/sync/end | safe end; HTTP reply is sent before server stops |
| POST | /api/clock | form fields unix, timezone (POSIX TZ), ntp=0/1; optional ntp_server when advertised |

The actual 0.10.6 firmware identifies a theme by URI `/api/theme`, not by a `theme=1` query flag on `/api/fs/upload`. Desktop 1.1 therefore uses `/api/theme?size=...&crc=...&replace=1` for strict theme transfers. The old theme route without size/CRC is retained only for legacy firmware. Legacy /upload, /api/smoothing, /api/theme/reset, /time and /api/info remain. Old INFO fields sd_total_mb/sd_used_mb are understood by Desktop. Version 4 write operations must return HTTP success and JSON `{"ok":true}`; a generic HTTP 200 page is not a commit acknowledgement. Write error response bodies carry the error code; stat not found is 404, IO read failure 500, conflict 409, strict upload rejection 422.

## Clock and device NTP

Device INFO reports `clock_unix`, `timezone`, `ntp_enabled` and, with the extension, `ntp_server`. `clock_sync` and `ntp` are original capabilities. The additive `ntp_server` capability enables an optional server field, without changing MPOD1 v4 or OTA v1. Original USB command: `MPOD1 X CLOCK <unix> <timezoneHex> <ntp0or1>`. Extended firmware also accepts a final `<ntpServerHex>`. Old clients can omit it; the stored server is retained. HTTP `/api/clock` accepts the same data as URL-encoded form fields, with optional `ntp_server`. Acknowledgements remain `OK CLOCK_SET` and HTTP `{"ok":true}`.

Time is UTC Unix seconds, 1600000000–4102444800. Device timezone is POSIX TZ, not a Windows timezone ID. Desktop translates the current Windows adjustment rule, including recurring DST transitions; manual selection uses Windows timezone names. NTP hostname/IPv4 is ASCII, at most 253 bytes, labels at most 63 bytes, with no URL, port, path or control characters. NTP/server/timezone settings persist in `monopod-clock` NVS. Server changes restart SNTP. It starts only in connected station mode, not an isolated access point. The default pool.ntp.org keeps time.google.com as fallback; a custom server is used on its own.

POSIX offsets use the opposite sign to civil UTC offsets: Moscow UTC+03:00 is encoded as `UTC-3`. Never add the timezone offset to the Unix timestamp before sending CLOCK. Desktop 1.2.1 decodes the actual returned timezone when showing local device time; an unrecognized timezone is explicitly shown as UTC rather than using an unsent UI selection.

## Wi-Fi OTA v1 (firmware 0.10.6.6)

OTA uses the existing local HTTP client with proxies and redirects disabled. It is independent of file protocol v4. `/api/ota/info` supplies live support, board, device ID, slot size, PIN requirement and busy/reboot status. Missing endpoint means an initial USB update; unsupported partition layout means USB installation of ota_0/ota_1/otadata is required. Desktop never writes a partition table or bootloader through OTA.

The client validates an application/sketch `.bin`: size, ESP 0xE9 header, chip ID (ESP32=0, ESP32-S3=9), first-segment ESP-IDF application descriptor, segment bounds, checksum and optional appended digest. Extra trailing data, merged/bootloader/partition images and signed containers are rejected. This is structural/integrity validation, not a cryptographic signature or proof of compatibility with custom wiring. The format follows the [Espressif image specification](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html) and installed ESP-IDF application descriptor definitions.

After explicit confirmation, Desktop rechecks the current ID/board/Sync state, opens the original file without write sharing, hashes those exact bytes, then streams one multipart field `firmware`, filename `firmware.bin`, MIME `application/octet-stream`. Content-Length includes prefix, original file and closing boundary. Headers: `X-MonoPod-OTA-PIN` (six screen digits), `X-MonoPod-OTA-Size` (decimal bytes), `X-MonoPod-OTA-SHA256` (64 lowercase hex). The PIN is never placed in the URL, preferences, log or command line. HTTP errors are reduced to safe codes without echoing the entered PIN.

Upload inactivity budget is 60 seconds and resets on written chunks. After the complete multipart body, the acknowledgement budget is 90 seconds. Only HTTP 200 with all of `ok:true`, `status:"verified"`, `rebooting:true` is accepted. Progress reaches 100% on sent image bytes and does not imply acceptance. Connection loss, malformed acknowledgement, cancellation or timeout after starting POST gives an unknown outcome; no automatic retry or rollback claim follows.

One firmware operation owns the connection and blocks all file/clock/sync requests until its lease is released in finally. Pending verification stores only board, ID, prior firmware, acceptance status and timestamp. After reboot the user reopens Wi-Fi Sync, reconnects, and Desktop compares actual `/api/info` firmware and `/api/ota/info` ID/board. It does not compare versions with the BIN filename. An unchanged version cannot prove installation of a particular image; even a changed version is not an on-device binary hash check.

## Paths and replacement

Canonical paths are absolute, slash-separated, <=240 UTF-8 bytes, component <=90 bytes. Reject dot components, leading dots, controls, Windows/FAT-reserved punctuation and trailing dot/space. /iPod_Control, System Volume Information and recycle/system areas are protected; root, /Music, /Playlists, /Themes and /MonoPod cannot be deleted or renamed. Version 0.10.6 permits listing /MonoPod and ordinary access to the explicit /MonoPod/badapple.bav asset; other private files remain protected. RMDIR never recurses. A dedicated theme operation is the only write exception for /MonoPod/theme.mpt. General file manager cannot edit internal settings or flash.

Transaction files: `/.monopod-transfer.part`, `/.monopod-transfer.bak`, `/.monopod-transfer.target`. Verify complete length/CRC and (for themes) structure. Write and flush target journal, rename original to backup, rename validated part to target, activate theme if applicable, then remove backup/journal. On a recoverable rename failure restore backup. At boot or next transfer, missing target + backup restores old file; existing committed target allows backup cleanup. Old v1–v3 music journals are also recovered. This prevents logical deletion of the old file before verification; it is not a guarantee against arbitrary FAT corruption or unflushed hardware caches during power failure. Retain backups of valuable media.

## MPT1 and MPT2 byte layout (little endian)

| Offset | MPT1 | MPT2 |
|---|---|---|
| 0..3 | ASCII MPT1 | ASCII MPT2 |
| 4 | version=1 | version=2 |
| 5 | flags=1, shared baseline | flags=1 |
| 6,7 | mainHeight=16, smallHeight=12 | same |
| 8,10 | uint16 main/small glyph counts, 1–256 | same |
| 12 | 4 uint16 RGB565 colors (8 bytes) | 12 uint16 colors (24 bytes) |
| 20 / 36 | uint32 payload length | uint32 payload length |
| 24 / 40 | uint32 CRC32(payload) | uint32 CRC32(payload) |
| 28 / 44 | main glyph table, then small | same |

Color order: Background, Foreground, Selection Background, Muted Text, Selection Foreground, Icons, Active Icons, Accent, Status Bar Text, Status Bar Icons, Progress Bar, Borders. MPT1 inherits selected foreground from Background and new foreground-like slots from Foreground. Palette is outside the legacy payload CRC; modifying a legal RGB565 palette does not invalidate it. Transfer CRC protects the entire uploaded file, including header/palette.

Glyph: uint32 Unicode scalar, uint8 width(1–24), uint8 advance(1–32), uint8 reserved=0, followed by ceil(width*height/2) coverage bytes. First pixel is the high nibble. Codepoints must be valid Unicode scalars, strictly increasing within each table. Maximum payload 131072 bytes; strict header/length/CRC/bounds validation precedes activation. Unknown version/flag, duplicate scalar, surrogate, truncated or trailing data is rejected. Coverage always stores 4bpp; runtime smoothing OFF thresholds alpha>=8. ON blends in the RGB565 channel domain using `(back*(15-a)+front*a+7)/15`. No reupload is required to toggle smoothing.


## Desktop timeout and cancellation policy

USB ordinary response lines have a whole-line 12-second deadline, including slowly arriving bytes. SCAN and SYNC END allow up to 5 minutes for index rebuilding. Serial reads are bounded and cancellation is checked on the worker. HTTP connection timeout is 8 seconds; ordinary RPC budget is 20 seconds; scan/end allow 5 minutes. Uploads use a 60-second inactivity budget reset as each chunk is sent, with no short total transfer deadline. Cancellation remains available during these waits. Device-side receive timeout remains 15 seconds.

Desktop 1.1.1 and later use up to 20 seconds for USB NEXT and 60 seconds for final SAVED. Logical blocks remain 1024 bytes, but physical writes are paced in chunks up to 64 bytes with an 8 ms pause, for compatibility with the original 256-byte HWCDC receive queue. After a poisoned stream closes, the UI waits until 18 seconds after the last write, opens a fresh connection and verifies HELLO/INFO. On a transport failure or explicit disconnect, it attempts END with a bounded cleanup deadline and closes the connection. No text is appended to the old binary receiver and no upload is automatically retried. The result of recovery is reported explicitly.

Desktop 1.1.2 treats user cancellation separately: if the stream is healthy, the current port stays open; otherwise it performs the same quiet-period handshake, restores the pre-cancel Sync state and retains the new link. Active USB Sync is confirmed with idempotent SYNC START followed by INFO. Recovery uses an independent 90-second cancellation budget, since the transfer token has already been cancelled. A folder refresh skips metadata reads to avoid immediately starting another binary transfer. A failed handshake is reported as a disconnected state. Explicit Disconnect or application close still releases Sync and the port. Cancellation never automatically restarts the file or queue.

Cancellation and transport loss do not prove that the final commit was undone. If the last acknowledgement was lost, reconnect and inspect the target before retrying. The transaction protects the old target until validation, but cannot reverse a commit that already completed.
