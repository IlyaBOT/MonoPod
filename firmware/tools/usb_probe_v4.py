#!/usr/bin/env python3
"""Read-only/native USB CDC MPOD1 v4 probe, optionally start/end Sync Mode.

Requires: python -m pip install pyserial
NO music/file writes. Native USB cable GPIO19/20, not USB-UART bridge.
"""
import argparse
import json
import time


def transact(ser, cmd, timeout=6.0):
    ser.reset_input_buffer()
    payload = ('MPOD1 ' + cmd + '\n').encode('ascii')
    print('  TX:', payload.rstrip().decode('ascii'))
    if ser.write(payload) != len(payload):
        raise RuntimeError('USB CDC short write')
    ser.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = ser.readline()
        if not data:
            continue
        line = data.decode('utf-8', 'replace').strip()
        print('  RX:', line[:700])
        if not line.startswith('MPOD1 '):
            continue
        reply = line[6:]
        if reply.startswith('ERR '):
            raise RuntimeError('MonoPod error: ' + reply)
        return reply
    raise TimeoutError(f'No MPOD1 response to {cmd!r}. Check native USB, CDC on boot, DTR, and port contention.')


def main():
    p = argparse.ArgumentParser(description='MonoPod native USB MPOD1 v4 handshake')
    p.add_argument('--port', help='Native USB CDC port (COM11 / /dev/cu.usbmodem...)')
    p.add_argument('--cycle', action='store_true', help='Enter and leave Sync Mode; requires microSD')
    p.add_argument('--timeout', type=float, default=6)
    args = p.parse_args()
    import serial
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    for port in ports:
        print('PORT:', port.device, port.description, f'VID:PID={port.vid!s}:{port.pid!s}', 'SN=', port.serial_number)
    if not args.port:
        print('Select native ESP32-S3 USB port using --port; no port was opened.')
        return
    with serial.Serial(args.port, baudrate=115200, timeout=.2, write_timeout=10,
                       rtscts=False, dsrdtr=False) as ser:
        # For USB CDC baudrate is only line-coding metadata; DTR is significant.
        ser.dtr = True
        ser.rts = False
        print('Opened', args.port, 'DTR=', ser.dtr, '(USB CDC, not UART baud-limited)')
        time.sleep(.5)
        hello = transact(ser, 'HELLO', args.timeout)
        if not hello.startswith('READY 4 SD='):
            raise RuntimeError('Unexpected protocol/version: ' + repr(hello))
        info = transact(ser, 'X INFO', args.timeout)
        if not info.startswith('OK '):
            raise RuntimeError('Unexpected INFO: ' + info)
        details = json.loads(info[3:])
        print('Protocol', details.get('protocol'), 'firmware', details.get('firmware'),
              'SD ready', details.get('sd_ready'), 'mode', details.get('sync_mode'))
        if args.cycle:
            started = False
            try:
                r = transact(ser, 'SYNC START', args.timeout)
                if r != 'OK SYNC_STARTED':
                    raise RuntimeError('SYNC START: ' + r)
                started = True
                print('Sync Mode started. USB transport is operational.')
            finally:
                if started:
                    # Full SD rescan may take longer than the handshake.
                    r = transact(ser, 'SYNC END', max(args.timeout, 30))
                    if r != 'OK SYNC_ENDED':
                        raise RuntimeError('SYNC END: ' + r)
                    print('Sync Mode closed.')


if __name__ == '__main__':
    try:
        main()
    except (Exception) as exc:
        raise SystemExit('ERROR: ' + str(exc))
