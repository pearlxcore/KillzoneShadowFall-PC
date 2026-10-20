"""Bounded read-only snapshots of explicitly supplied character object addresses.

Uses PROCESS_VM_READ only. Does not inject, invoke guest code, alter protection,
write process memory, suspend threads, or enumerate unrelated process memory.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import struct


class Reader:
    def __init__(self, pid):
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.ReadProcessMemory.argtypes = (wintypes.HANDLE, ctypes.c_void_p,
                                             ctypes.c_void_p, ctypes.c_size_t,
                                             ctypes.POINTER(ctypes.c_size_t))
        self.api.ReadProcessMemory.restype = wintypes.BOOL
        self.api.CloseHandle.argtypes = (wintypes.HANDLE,)
        self.handle = self.api.OpenProcess(0x10, False, pid)  # PROCESS_VM_READ
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, address, size):
        if address < 0x10000 or size < 0 or size > 65536 or address + size > 2**64:
            return None
        data = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        if not self.api.ReadProcessMemory(self.handle, address, data, size, ctypes.byref(count)) or count.value != size:
            return None
        return data.raw

    def pointer(self, address):
        data = self.read(address, 8)
        return struct.unpack('<Q', data)[0] if data else 0

    def engine_string(self, address):
        header = self.read(address, 16)
        if not header:
            return None
        length = struct.unpack_from('<i', header, 8)[0]
        if not 0 <= length <= 128:
            return None
        data = self.read(address + 16, length)
        if data is None or any(c < 32 or c > 126 for c in data):
            return None
        return data.decode('ascii')

    def identity(self, address):
        table = self.pointer(address)
        getter = self.pointer(table)
        code = self.read(getter, 8)
        result = {'address': hex(address)}
        if not code or code[:3] != bytes.fromhex('488d05') or code[7] != 0xc3:
            return result
        rtti = getter + 7 + struct.unpack_from('<i', code, 3)[0]
        result.update(vtable=hex(table), getter=hex(getter), rtti=hex(rtti))
        # RTTI registration stores the human-readable class name at58.
        text = self.read(self.pointer(rtti + 0x58), 128)
        if text:
            text = text.split(b'\0', 1)[0]
            if text and all(32 <= c <= 126 for c in text):
                result['type_name'] = text.decode('ascii')
        name = self.engine_string(self.pointer(address + 0x10))
        if name is not None:
            result['resource_name'] = name
        return result

    def snapshot(self, address, size):
        data = self.read(address, size)
        result = self.identity(address)
        result['bytes_requested'] = size
        result['readable'] = data is not None
        if data is None:
            return result
        result['raw_hex'] = data.hex()
        result['qwords'] = [{'offset': hex(i), 'value': hex(struct.unpack_from('<Q', data, i)[0])}
                            for i in range(0, len(data) - 7, 8)]
        result['referenced_objects'] = []
        seen = set()
        for item in result['qwords']:
            pointer = int(item['value'], 16)
            if pointer in seen:
                continue
            seen.add(pointer)
            identity = self.identity(pointer)
            if 'rtti' in identity:
                result['referenced_objects'].append({'offset': item['offset'], **identity})
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--address', type=lambda value: int(value, 0), action='append', required=True)
    parser.add_argument('--size', type=int, default=1024)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 8 <= args.size <= 4096 or len(args.address) > 16:
        parser.error('Use 8..4096 bytes and at most 16 explicit addresses.')
    reader = Reader(args.pid)
    try:
        report = {'pid': args.pid, 'read_only': True,
                  'atomic': False, 'objects': [reader.snapshot(p, args.size) for p in args.address]}
    finally:
        reader.api.CloseHandle(reader.handle)
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text + '\n', encoding='utf-8')
    else:
        print(text)


if __name__ == '__main__':
    main()
