"""Regression for enormous PICTURE blocks without retaining copyrighted fixtures."""
from pathlib import Path
import shutil
import subprocess
import pytest

BASE = Path(__file__).resolve().parent


def test_picture_skip_decoder_does_not_read_artwork(tmp_path):
    if not shutil.which('g++') or not shutil.which('flac'):
        pytest.skip('g++/flac unavailable')
    original = (BASE / 'sample_flac_native_test.flac').read_bytes()
    assert original[:4] == b'fLaC'
    assert original[4] == 0 and original[5:8] == b'\0\0"' # STREAMINFO is 34 bytes
    assert original[42] & 0x7F != 6
    artwork_bytes = 9 * 1024 * 1024 + 20
    large = original[:42] + b'\x06' + artwork_bytes.to_bytes(3,'big') + bytes(artwork_bytes) + original[42:]
    path = tmp_path / 'picture.flac'
    no_cover = tmp_path / 'reference.flac'
    virtual = tmp_path / 'virtual.flac'
    binary = tmp_path / 'picture_test'
    path.write_bytes(large)
    no_cover.write_bytes(original)
    subprocess.run(['g++','-std=c++17','-O2','-I',str(BASE/'fakes'),'-o',str(binary),str(BASE/'flac_picture_skip_native_test.cpp')],check=True)
    subprocess.run([str(binary),str(path),str(no_cover),str(virtual)],check=True)
    subprocess.run(['flac','--silent','--test',str(virtual)],check=True)
