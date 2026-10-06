#!/usr/bin/env python3
"""Generate owned synthetic media; committed fixture hashes are qualification inputs.

Regeneration is explicit: encoder versions can change compressed bytes. CI uses
committed media and validates SHA-256; it never regenerates or downloads tracks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]


def signal(rate, milliseconds):
    pcm = bytearray()
    for frame in range(rate * milliseconds // 1000):
        within = frame % rate
        active = rate // 4 <= within < 3 * rate // 4
        period = rate // 440
        phase = (within - rate // 4) % period
        left = ((phase * 28000 // period) - 7000 if phase < period // 2 else
                21000 - phase * 28000 // period) if active else 0
        pcm.extend(struct.pack('<hh', left, -left))
    return bytes(pcm)


def wav_file(path, rate, milliseconds):
    with wave.open(str(path), 'wb') as w:
        w.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        w.writeframes(signal(rate, milliseconds))


def pdb_file(path):
    data = bytearray(1024)
    struct.pack_into('<II', data, 4, 512, 1)
    struct.pack_into('<I', data, 36, 1)
    struct.pack_into('<I', data, 524, 0xffffffff)
    struct.pack_into('<I', data, 536, 1)
    row = 552
    struct.pack_into('<H', data, row, 0x24)
    struct.pack_into('<I', data, row + 0x48, 123)
    data[1020] = 1
    for index, offset, value in [(17, 160, b'Qualification title'),
                                  (18, 195, b'WRONG INDEX 18'),
                                  (20, 230, b'/Contents/onset-44100.wav')]:
        struct.pack_into('<H', data, row + 0x5e + 2 * index, offset)
        data[row + offset] = ((len(value) + 1) << 1) | 1
        data[row + offset + 1:row + offset + 1 + len(value)] = value
    path.write_bytes(data)


def anlz_file(path):
    audio = ('/Contents/onset-44100.wav' + '\x00').encode('utf-16-be')
    ppth = b'PPTH' + struct.pack('>IIII', 20, 20 + len(audio), 0, len(audio)) + audio
    beats = b''.join(struct.pack('>HHI', phase, 12000, (phase - 1) * 500) for phase in range(1, 5))
    pqtz = b'PQTZ' + struct.pack('>IIIII', 24, 24 + len(beats), 0, 0x80000, 4) + beats
    cue = bytearray(56)
    cue[:4] = b'PCPT'
    struct.pack_into('>III', cue, 4, 28, 56, 1)
    struct.pack_into('>HH', cue, 24, 0xffff, 0xffff)
    cue[28] = 1
    struct.pack_into('>I', cue, 32, 1250)
    pcob = b'PCOB' + struct.pack('>II', 24, 24 + len(cue)) + struct.pack('>IHHI', 1, 0, 1, 0) + cue
    payload = ppth + pqtz + pcob
    path.write_bytes(b'PMAI' + struct.pack('>II', 28, 28 + len(payload)) + bytes(16) + payload)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=ROOT / 'tests/fixtures/p4')
    ap.add_argument('--ffmpeg', default=shutil.which('ffmpeg'))
    args = ap.parse_args()
    if not args.ffmpeg:
        ap.error('ffmpeg is required only for explicit fixture generation')
    args.output.mkdir(parents=True, exist_ok=True)
    wav_file(args.output / 'onset-44100.wav', 44100, 6500)
    wav_file(args.output / 'short-48000.wav', 48000, 80)
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / 'onset-48000.wav'
        wav_file(source, 48000, 6500)
        for input_path, output, options in [
            (source, 'onset-48000.flac', ['-c:a', 'flac', '-compression_level', '8']),
            (args.output / 'onset-44100.wav', 'onset-44100.mp3', ['-c:a', 'libmp3lame', '-q:a', '4'])]:
            subprocess.run([args.ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', '-y',
                '-i', str(input_path), '-map_metadata', '-1', '-fflags', '+bitexact',
                '-flags:a', '+bitexact'] + options + [str(args.output / output)], check=True)
    pdb_file(args.output / 'export.pdb')
    anlz_file(args.output / 'ANLZ0000.DAT')
    names = ('onset-44100.wav', 'short-48000.wav', 'onset-48000.flac', 'onset-44100.mp3',
             'export.pdb', 'ANLZ0000.DAT')
    version = subprocess.run([args.ffmpeg, '-version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    manifest = dict(schema=1, origin='Owned deterministic synthetic stereo pulses; no third-party audio',
        encoder=version, files={name: dict(size=(args.output / name).stat().st_size,
        sha256=hashlib.sha256((args.output / name).read_bytes()).hexdigest()) for name in names})
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
