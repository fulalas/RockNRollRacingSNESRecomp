"""Emit src/rnrr_fps30_patch.h from the byte diff between the stock ROM and the
30 FPS hack, or apply that diff to a ROM file (--apply <in> <out>)."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent


def runs(stock: bytes, hack: bytes):
    assert len(stock) == len(hack)
    out = []
    i = 0
    while i < len(stock):
        if stock[i] == hack[i]:
            i += 1
            continue
        j = i
        while j < len(stock) and (stock[j] != hack[j] or (j + 1 < len(stock) and stock[j + 1] != hack[j + 1])):
            j += 1
        out.append((i, stock[i:j], hack[i:j]))
        i = j
    return out


def emit(stock: bytes, hack: bytes, path: pathlib.Path):
    rs = runs(stock, hack)
    lines = ["#ifndef RNRR_FPS30_PATCH_H", "#define RNRR_FPS30_PATCH_H", "", "#include <stdint.h>", "",
             "typedef struct { uint32_t off; uint16_t len; const uint8_t *old; const uint8_t *new_; } RnrrFps30Run;", ""]
    for n, (off, old, new) in enumerate(rs):
        lines.append(f"static const uint8_t k_f30_old_{n}[] = {{{', '.join('0x%02x' % b for b in old)}}};")
        lines.append(f"static const uint8_t k_f30_new_{n}[] = {{{', '.join('0x%02x' % b for b in new)}}};")
    lines += ["", "static const RnrrFps30Run kRnrrFps30Runs[] = {"]
    for n, (off, old, new) in enumerate(rs):
        lines.append(f"    {{0x{off:06x}, {len(old)}, k_f30_old_{n}, k_f30_new_{n}}},")
    lines += ["};", "", "#endif", ""]
    path.write_text("\n".join(lines))
    print(f"{path}: {len(rs)} runs, {sum(len(o) for _, o, _ in rs)} bytes")


def apply(stock: bytes, hack: bytes, src: pathlib.Path, dst: pathlib.Path):
    rom = bytearray(src.read_bytes())
    if len(rom) % 1024 == 512:
        rom = rom[512:]
    for off, old, new in runs(stock, hack):
        if bytes(rom[off:off + len(old)]) != old:
            raise SystemExit(f"{src}: bytes at {off:#x} are not the stock ROM's")
        rom[off:off + len(new)] = new
    dst.write_bytes(bytes(rom))


def main() -> int:
    stock = (HERE / "roms" / "ROCK.smc")
    hack = (HERE / "roms" / "ROCK30.smc")
    args = sys.argv[1:]
    if args[:1] == ["--roms"]:
        stock, hack = pathlib.Path(args[1]), pathlib.Path(args[2])
        args = args[3:]
    s, h = stock.read_bytes(), hack.read_bytes()
    if args[:1] == ["--apply"]:
        apply(s, h, pathlib.Path(args[1]), pathlib.Path(args[2]))
        return 0
    emit(s, h, HERE / "src" / "rnrr_fps30_patch.h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
