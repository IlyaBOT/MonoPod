# Architecture

`MonoPod.Core` targets net8.0. Models, canonical paths, incremental CRC32, transport contract, two independent transport implementations, bounded audio metadata parser and binary MPT codec have no dependency on Forms.

`UsbMonoPodTransport` owns a SerialPort adapter. Calls are serialized by a semaphore and synchronous serial operations run on a worker with bounded timeouts. `ISerialLink` enables the stateful protocol emulator. A binary-stream failure invalidates and closes the link; it never appends textual cleanup to unfinished data. Reconnect and retry from the beginning after device timeout; partial files are not resumed.

`WiFiMonoPodTransport` uses HttpClient, multipart streaming and chunked range reads. It validates per-response CRC32. HTTP write cancellation can leave a pending device-side temporary file until the 15-second idle timeout; the previous committed destination remains untouched.

`Downloads` writes a unique local `.part`, checks each CRC and final remote stat, flushes, then replaces the local file. Remote upload CRC is computed in a streaming pass before transmission. Memory use is bounded independently of music file size.

`MetadataReader` reads no more than 256 KiB per file and seeks around images/audio payloads. It reads ID3v2.3/v2.4 text (Latin-1/UTF-8/UTF-16), ID3v1 fallback, FLAC STREAMINFO/Vorbis comments, PCM WAV headers, MPEG frame properties and Xing/VBRI counts. It reports authoritative durations from TLEN, frame counts, FLAC samples or WAV data length. CBR file-size duration estimates are not displayed. Raw AAC properties can be shown; its duration remains unknown. Unsupported/encrypted/unsynchronised tags remain blank or carry a note. Remote cache keys contain path, size and modification timestamp; mutations and reconnect clear it. With FAT timestamp granularity, manual refresh/reconnect is required after an external same-size/same-time modification.

`MonoPod.Desktop` contains actual WinForms MenuStrip/ComboBox/TabControl/DataGridView/TreeView/StatusStrip/ProgressBar controls. MainForm coordinates operations, cancellation and connection lifecycle. No wire parsing lives in button handlers. ThemeEditorForm handles user editing; ThemeRasterizer produces coverage records from a local TTF/OTF with 4× sampling and previews those exact records using firmware RGB565 blending. Color profiles include glyph data when explicitly saved. Settings are atomically saved to `%AppData%\MonoPod\Desktop\settings.json`; no Wi-Fi passwords are requested or logged.

Device `SyncFiles.cpp` is shared by USB and HTTP. One global transfer transaction uses a 1 KiB USB / HTTP working buffer, incremental CRC and a recovery journal on SD. Generic user file APIs cannot write /MonoPod, /iPod_Control or system directories. Theme installation has a dedicated path exception and validates theme structure/CRC before replacement. Playback and normal UI remain excluded by SyncController ownership.

`MonoPod.Desktop.Tests` uses xUnit, a stateful MPOD1 emulator and a real loopback HttpListener. Native MSVC tests compile the actual SyncFiles.cpp and Theme.cpp with an isolated filesystem adapter. These tests do not emulate SD electrical behavior, power-loss atomicity of FAT writes, ESP32 runtime scheduling or physical USB.

FLAC STREAMINFO total sample count zero is treated as unknown, per [RFC 9639 section 8.2](https://www.rfc-editor.org/rfc/rfc9639.html#section-8.2); it is not displayed as a confirmed zero duration.


HTTP has an 8-second connect timeout and 20-second ordinary RPC budget. Upload timeout measures 60 seconds of inactivity and resets after every sent chunk, so a slow progressing file is not rejected by a short total timeout. USB has a 12-second whole-line deadline. Reindexing during scan/end has a cancelable 5-minute budget on both transports. Window close keeps the app busy while it safely ends Sync; if binary framing is uncertain the port closes instead of appending a cleanup command.
