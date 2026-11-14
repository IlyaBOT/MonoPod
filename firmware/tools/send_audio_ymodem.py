#!/usr/bin/env python3
"""MonoPod standalone ESP32-S3 diagnostic audio uploader.

USB-UART / Serial0 @ 115200 (default). Convert audio to raw PCM16LE with ffmpeg,
then transfer it via standard YMODEM-CRC (block0, 1K blocks, double EOT,
empty block0). The MCU plays it exactly once from PSRAM.
"""
from __future__ import annotations

import argparse
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SOH, STX, EOT, ACK, NAK, CAN, CRC_REQ = 1, 2, 4, 6, 21, 24, ord("C")
PAD = 0x1A


def crc16(data: bytes) -> int:
    c = 0
    for x in data:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ (0x1021 if c & 0x8000 else 0)) & 0xFFFF
    return c


def packet(seq: int, payload: bytes, block_length: int) -> bytes:
    if len(payload) > block_length or block_length not in (128, 1024):
        raise ValueError("invalid YMODEM packet")
    data = payload.ljust(block_length, bytes([PAD]))
    return bytes([SOH if block_length == 128 else STX, seq & 255, (255 - seq) & 255]) + data + struct.pack(">H", crc16(data))


def make_tone(path: Path, rate: int, channels: int, seconds: float, frequency: float) -> None:
    if not 0 < seconds <= 60:
        raise ValueError("--tone-seconds must be >0 and <=60")
    with path.open("wb") as f:
        for t in range(int(rate * seconds)):
            sample = int(8000 * math.sin(2 * math.pi * frequency * t / rate))
            f.write(struct.pack("<h", sample) * channels)


def convert_audio(source: Path, path: Path, rate: int, channels: int) -> None:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg not found in PATH; use --tone or install ffmpeg")
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
           "-vn", "-sn", "-ac", str(channels), "-ar", str(rate),
           "-acodec", "pcm_s16le", "-f", "s16le", str(path)]
    subprocess.run(cmd, check=True)


def until(ser, expected: int, timeout: float, label: str, also: tuple[int, ...] = ()) -> int:
    deadline = time.monotonic() + timeout
    tail = bytearray()
    while time.monotonic() < deadline:
        ch = ser.read(1)
        if not ch:
            continue
        value = ch[0]
        if value in (expected,) + also:
            return value
        if value == CAN:
            raise RuntimeError(f"receiver cancelled at {label}")
        tail.extend(ch)
        if len(tail) > 150:
            del tail[:100]
    preview = tail.decode(errors="replace")
    raise TimeoutError(f"timeout waiting {label}; recent data: {preview!r}")


def send_packet(ser, seq: int, payload: bytes, length: int, retries: int = 10) -> None:
    frame = packet(seq, payload, length)
    for retry in range(retries):
        ser.write(frame)
        response = until(ser, ACK, 7.0, f"ACK seq={seq}", also=(NAK,))
        if response == ACK:
            return
        print(f"[YMODEM] retry seq={seq} attempt={retry+1}")
    raise RuntimeError(f"receiver repeatedly NAK'd block {seq}")


def perform_transfer(ser, raw: bytes, rate: int, channels: int, block_size: int = 128) -> None:
    if len(raw) == 0 or len(raw) % (2 * channels):
        raise ValueError("PCM must contain an integral number of frames")
    ser.reset_input_buffer()
    ser.write(f"UPLOAD={rate},{channels}\n".encode("ascii"))
    until(ser, CRC_REQ, 10.0, "initial YMODEM C")
    print("[YMODEM] receiver ready")
    name = b"audio.pcm"
    header = name + b"\0" + str(len(raw)).encode("ascii") + b"\0"
    send_packet(ser, 0, header, 128)
    until(ser, CRC_REQ, 5.0, "C after block0")
    if block_size not in (128, 1024):
        raise ValueError("--block-size must be 128 or 1024")
    packets = (len(raw) + block_size - 1) // block_size
    for i in range(packets):
        send_packet(ser, (i + 1) % 256, raw[i*block_size:(i+1)*block_size], block_size)
        if i == 0 or (i+1) % 32 == 0 or i == packets - 1:
            print(f"[YMODEM] {i+1}/{packets} blocks ({(i+1)*100//packets}%)", flush=True)
    ser.write(bytes([EOT]))
    until(ser, NAK, 6.0, "first EOT NAK")
    ser.write(bytes([EOT]))
    until(ser, ACK, 6.0, "second EOT ACK")
    until(ser, CRC_REQ, 6.0, "C requesting empty block0")
    send_packet(ser, 0, b"\0", 128)
    print("[YMODEM] transfer confirmed; ESP32 plays audio once")


def main() -> int:
    ap = argparse.ArgumentParser(description="Send mono/stereo PCM16 over YMODEM-CRC to MonoPod S3 DevTest")
    ap.add_argument("--port", required=True, help="Windows COM port of USB-UART connector (e.g. COM7)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--block-size", type=int, choices=(128, 1024), default=128,
                    help="YMODEM payload size; 128 is robust, 1024 is faster")
    audio = ap.add_mutually_exclusive_group(required=True)
    audio.add_argument("--file", type=Path, help="MP3/M4A/FLAC/WAV/etc, ffmpeg required")
    audio.add_argument("--tone", action="store_true", help="generate diagnostic 896 Hz tone without ffmpeg")
    ap.add_argument("--rate", type=int, default=16000, help="8000..48000 Hz; 16000 conserves PSRAM")
    ap.add_argument("--channels", type=int, choices=(1, 2), default=1)
    ap.add_argument("--tone-seconds", type=float, default=2.0)
    ap.add_argument("--frequency", type=float, default=896.0)
    ap.add_argument("--monitor", type=int, default=4, help="seconds to read console after transfer")
    args = ap.parse_args()
    if not 8000 <= args.rate <= 48000:
        ap.error("--rate must be 8000..48000")
    try:
        import serial
    except ImportError:
        print("Install dependency: py -m pip install pyserial", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="monopod_s3_") as folder:
        path = Path(folder) / "audio.pcm"
        if args.tone:
            make_tone(path, args.rate, args.channels, args.tone_seconds, args.frequency)
        else:
            if not args.file.is_file():
                ap.error(f"audio file not found: {args.file}")
            convert_audio(args.file, path, args.rate, args.channels)
        raw = path.read_bytes()
        duration = len(raw) / (args.rate * args.channels * 2)
        print(f"PCM16LE: {len(raw):,} bytes, {args.rate} Hz, {args.channels} channel(s), {duration:.1f} s")
        with serial.Serial(args.port, baudrate=args.baud, timeout=0.1, write_timeout=10) as ser:
            # Opening native USB-CDC on some boards resets them; using the USB-UART bridge avoids this.
            time.sleep(0.3)
            try:
                perform_transfer(ser, raw, args.rate, args.channels, block_size=args.block_size)
            except (RuntimeError, TimeoutError):
                # Cancel at the YMODEM frame boundary and collect MCU diagnostics.
                try:
                    ser.write(bytes([CAN, CAN]))
                    ser.flush()
                    deadline = time.monotonic() + 1.5
                    tail = bytearray()
                    while time.monotonic() < deadline:
                        chunk = ser.read(256)
                        if chunk:
                            tail.extend(chunk)
                    if tail:
                        print("[YMODEM] receiver diagnostics after abort:",
                              tail.decode("utf-8", errors="replace").strip(), file=sys.stderr)
                except OSError:
                    pass
                raise
            stop = time.monotonic() + args.monitor
            while time.monotonic() < stop:
                line = ser.readline()
                if line:
                    print(line.decode("utf-8", errors="replace").rstrip())
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, TimeoutError, subprocess.CalledProcessError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
