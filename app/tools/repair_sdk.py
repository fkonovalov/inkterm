from pathlib import Path
import struct
import sys


def repair(data):
    data = bytearray(data)
    if data[:6] != b'\x7fELF\x01\x01':
        return data, 0
    shoff = struct.unpack_from('<I', data, 32)[0]
    entsize, count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, shoff + i * entsize) for i in range(count)]
    changed = 0
    for section in sections:
        if section[1] != 11:
            continue
        for offset in range(section[4], section[4] + section[5], section[9]):
            _, value, _, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
            if index == 0 or index >= 0xff00 or info & 15 == 6:
                continue
            actual = next((i for i, s in enumerate(sections) if s[2] & 2 and s[3] <= value < s[3] + s[5]), None)
            if actual is not None and actual != index:
                struct.pack_into('<H', data, offset + 14, actual)
                changed += 1
    return data, changed


def check():
    data = bytearray(256)
    data[:6] = b'\x7fELF\x01\x01'
    struct.pack_into('<I', data, 32, 64)
    struct.pack_into('<HH', data, 46, 40, 4)
    struct.pack_into('<10I', data, 104, 0, 8, 3, 4096, 0, 32, 0, 0, 4, 0)
    struct.pack_into('<10I', data, 144, 0, 1, 0, 0, 0, 16, 0, 0, 1, 0)
    struct.pack_into('<10I', data, 184, 0, 11, 2, 0, 224, 32, 0, 0, 4, 16)
    struct.pack_into('<IIIBBH', data, 224, 0, 4100, 4, 17, 0, 2)
    struct.pack_into('<IIIBBH', data, 240, 0, 0, 0, 17, 0, 0)
    fixed, count = repair(data)
    assert count == 1 and struct.unpack_from('<H', fixed, 238)[0] == 1
    assert fixed[240:] == data[240:]
    assert repair(fixed)[1] == 0
    print('ELF check passed: mutable symbol repaired, undefined symbol preserved, repeat is unchanged')


if __name__ == '__main__':
    if sys.argv[1:] == ['--check']:
        check()
    else:
        total = 0
        for p in Path(sys.argv[1]).rglob('*.so*'):
            if p.is_symlink() or not p.is_file() or p.name.endswith('.elf-backup'):
                continue
            original = p.read_bytes()
            fixed, count = repair(original)
            if count:
                p.with_suffix(p.suffix + '.elf-backup').write_bytes(original)
                p.write_bytes(fixed)
                total += count
        print(f'Repaired {total} dynamic symbol section indices')
