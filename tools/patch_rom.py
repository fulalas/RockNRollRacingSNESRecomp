"""Relocate the sprite emitter's screen-X reject test into free ROM space.

The stock emitter hides any sprite whose screen X is outside [-64, 256) and
only sets the OAM high-X bit for negative X. The relocated test keeps the same
shape but reads its two bounds from immediates the host rewrites at run time,
and sets the high-X bit for X >= 256 as well, so widescreen margins get real
sprites instead of wrapped ones. Usage: patch_rom.py <in.smc> <out.smc>, or
--c-table to print the patch as a C initializer.
"""
import sys

LEFT = (0xFFC0).to_bytes(2, "little")
RIGHT = (0x0100).to_bytes(2, "little")
W256 = (0x0100).to_bytes(2, "little")

R1 = (b"\xc9" + LEFT + b"\xb0\x0a" + b"\xc9" + RIGHT + b"\xb0\x0a" + b"\xc9" + W256 + b"\x90\x02"
      + b"\xe6\x9e" + b"\x85\xa0" + b"\x6b"
      + b"\xe6\x9e" + b"\xa9\x80\x00" + b"\x85\xa0" + b"\x6b")
R3 = (b"\xc9" + LEFT + b"\xb0\x0a" + b"\xc9" + RIGHT + b"\xb0\x19" + b"\xc9" + W256 + b"\x90\x0b"
      + b"\xe2\x30" + b"\x9d\x09\x0b" + b"\xa9\x01" + b"\x9d\x0f\x0b" + b"\x6b"
      + b"\xe2\x30" + b"\x9d\x09\x0b" + b"\x9e\x0f\x0b" + b"\x6b"
      + b"\xe2\x30" + b"\xa9\x01" + b"\x9d\x0f\x0b" + b"\xa9\x80" + b"\x9d\x09\x0b" + b"\x6b")
R4 = (b"\xc9" + LEFT + b"\xb0\x0a" + b"\xc9" + RIGHT + b"\xb0\x19" + b"\xc9" + W256 + b"\x90\x0b"
      + b"\xe2\x30" + b"\x9d\x21\x0b" + b"\xa9\x01" + b"\x9d\x27\x0b" + b"\x6b"
      + b"\xe2\x30" + b"\x9d\x21\x0b" + b"\x9e\x27\x0b" + b"\x6b"
      + b"\xe2\x30" + b"\xa9\x05" + b"\x9d\x2d\x0b" + b"\x6b")
assert len(R1) == 28 and len(R3) == 48 and len(R4) == 43, (len(R1), len(R3), len(R4))

PATCHES = [
    (0x01FF10, R1),
    (0x01FF30, R3),
    (0x01FF60, R4),
    (0x00D3A5, b"\x22\x10\xff\x01\x80\x0c"),
    (0x00D40A, b"\x22\x10\xff\x01\x80\x0c"),
    (0x00D632, b"\x22\x30\xff\x01\x80\x1f"),
    (0x00D6A1, b"\x22\x60\xff\x01\x60"),
]

EXPECT = {
    0x00D3A5: bytes.fromhex("c9c0ffb009c90001b0cc"),
    0x00D40A: bytes.fromhex("c9c0ffb009c90001b0cc"),
    0x00D632: bytes.fromhex("c9c0ffb016c900019007"),
    0x00D6A1: bytes.fromhex("c9c0ffb016c900019008"),
}


def lorom_offset(addr):
    return ((addr >> 16) & 0x7f) * 0x8000 + (addr & 0x7fff)


def apply(rom: bytearray) -> None:
    for addr, expect in EXPECT.items():
        off = lorom_offset(addr)
        if rom[off:off + len(expect)] != expect:
            raise SystemExit(f"unexpected bytes at {addr:06x}; not a supported Rock N' Roll Racing image")
    for addr, data in PATCHES:
        off = lorom_offset(addr)
        rom[off:off + len(data)] = data


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--c-table":
        for addr, data in PATCHES:
            print(f"    {{0x{addr:06x}, {len(data)}, {{{', '.join(f'0x{b:02x}' for b in data)}}}}},")
        return 0
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    rom = bytearray(open(sys.argv[1], "rb").read())
    if len(rom) % 1024 == 512:
        rom = rom[512:]
    apply(rom)
    open(sys.argv[2], "wb").write(rom)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
