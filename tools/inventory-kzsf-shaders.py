"""Inspect shaders in a user's dump; this does NOT compile a shader cache.

Development tool for first-run shader preparation. Decodes the observed Killzone
CB10C183 chunk container using bounded LZ4 decompression. Reports binary metadata
and source offsets, without writing game shader bytes or changing the dump.
"""
import argparse
import ctypes
import ctypes.util
import json
from pathlib import Path
import struct
import time

MAGIC = 0xCB10C183
FOOTER = b'OrbShdr'
MAX_CHUNK = 16 * 1024 * 1024
MAX_EXPANDED = 8 * 1024**3


class Lz4:
    def __init__(self, library=None):
        location = library or ctypes.util.find_library('lz4')
        if not location:
            raise OSError('Specify --lz4 with the path to the LZ4 shared library.')
        self.library = ctypes.CDLL(str(location))
        self.decode = self.library.LZ4_decompress_safe
        self.decode.argtypes = (ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
        self.decode.restype = ctypes.c_int

    def decompress(self, source, expected):
        if not 0 < expected <= MAX_CHUNK or not source or len(source) > MAX_CHUNK * 2:
            raise ValueError('LZ4 block exceeds supported bounds')
        destination = ctypes.create_string_buffer(expected)
        count = self.decode(source, destination, len(source), expected)
        if count != expected:
            raise ValueError(f'LZ4 size mismatch: {count}, expected {expected}')
        return destination.raw


def read_exact(stream, size):
    result = stream.read(size)
    if len(result) != size:
        raise ValueError('Truncated asset')
    return result


def blocks(path, lz4):
    """Yield (decoded offset, bytes), preserving block bounds for large assets."""
    with path.open('rb') as stream:
        header = stream.read(32)
        if len(header) < 4 or struct.unpack_from('<I', header)[0] != MAGIC:
            stream.seek(0)
            offset = 0
            while data := stream.read(1024 * 1024):
                yield offset, data
                offset += len(data)
            return
        if len(header) != 32:
            raise ValueError('Truncated compressed header')
        _, chunk, expanded = struct.unpack_from('<IIQ', header)
        if not 0 < chunk <= MAX_CHUNK or not 0 < expanded <= MAX_EXPANDED:
            raise ValueError('Unsupported compressed asset bounds')
        count = (expanded + chunk - 1) // chunk
        if count > 131072:
            raise ValueError('Too many compressed blocks')
        table = read_exact(stream, count * 4)
        sizes = struct.unpack('<' + 'I' * count, table)
        if any(not 0 < size <= MAX_CHUNK * 2 for size in sizes):
            raise ValueError('Invalid compressed block size')
        if 32 + len(table) + sum(sizes) != path.stat().st_size:
            raise ValueError('Compressed block table does not match file size')
        offset = 0
        for size in sizes:
            expected = min(chunk, expanded - offset)
            data = lz4.decompress(read_exact(stream, size), expected)
            yield offset, data
            offset += expected


def shader_records(path, lz4):
    carry = b''
    emitted = -1
    for offset, chunk in blocks(path, lz4):
        data = carry + chunk
        base = offset - len(carry)
        start = 0
        while True:
            position = data.find(FOOTER, start)
            if position < 0:
                break
            start = position + len(FOOTER)
            absolute = base + position
            if absolute <= emitted or position + 28 > len(data):
                continue
            flags = struct.unpack_from('<I', data, position + 8)[0]
            length = flags >> 8
            # Signature matches in compressed/literal data alone aren't proof of
            # valid code. Decode the container first and reject invalid lengths.
            if data[position + 7] > 16 or length < 4 or length % 4:
                continue
            emitted = absolute
            yield {'footer_offset': absolute, 'code_bytes': length,
                   'type': (flags >> 2) & 15,
                   'hash': f'{struct.unpack_from("<Q", data, position + 16)[0]:016x}'}
        carry = data[-64:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--lz4', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--limit', type=int, default=0, help='limit asset files for a bounded probe')
    args = parser.parse_args()
    lz4 = Lz4(args.lz4)
    files = [args.game / 'eboot.bin']
    assets = args.game / 'localcachepink/lumps'
    files += sorted(p for p in assets.iterdir() if p.is_file() and '.core' in p.name)
    if args.limit:
        files = files[:args.limit]
    shaders, failures = {}, []
    start = time.monotonic()
    for index, path in enumerate(files):
        try:
            for record in shader_records(path, lz4):
                record['file'] = path.relative_to(args.game).as_posix()
                shaders.setdefault(record['hash'], record)
        except (OSError, ValueError, struct.error) as error:
            failures.append({'file': path.relative_to(args.game).as_posix(), 'error': str(error)})
        if index % 25 == 0 or index + 1 == len(files):
            print(f'Discovery {index + 1}/{len(files)}: {len(shaders)} unique binary records, '
                  f'{len(failures)} unreadable assets', flush=True)
    result = {'format': 1, 'status': 'inventory only; not a compiled shader cache',
              'files_scanned': len(files), 'seconds': round(time.monotonic() - start, 3),
              'shaders': list(shaders.values()), 'failures': failures}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
