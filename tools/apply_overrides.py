"""Widen the sprite emitter's screen-X reject window and make CPU cycle
charges overclockable in the generated C."""
import argparse
import pathlib
import re

BLOCKS = ("0x01FF10", "0x01FF30", "0x01FF60")
INCLUDE = '#include "rnrr_ws_cull.h"\n'


def patch(path: pathlib.Path) -> int:
    text = path.read_text()
    lines = text.split("\n")
    patched = 0
    for block in BLOCKS:
        try:
            start = next(i for i, l in enumerate(lines) if f"cpu_trace_block(cpu, {block});" in l)
        except StopIteration:
            continue
        if any(f"cpu_trace_block(cpu, {block});" in l for l in lines[:start]):
            pass
        left = right = None
        if any("g_rnrr_cull_left" in l for l in lines[start:start + 80]):
            patched += 1
            continue
        for i in range(start, min(start + 80, len(lines))):
            if left is None and re.search(r"uint16 _v\d+ = 0xffc0;", lines[i]):
                left = i
            elif left is not None and re.search(r"uint16 _v\d+ = 0x100;", lines[i]):
                right = i
                break
        if left is None or right is None:
            raise SystemExit(f"{path}: block {block} does not match the expected emitter shape")
        lines[left] = re.sub(r"= 0xffc0;", "= g_rnrr_cull_left;", lines[left])
        lines[right] = re.sub(r"= 0x100;", "= g_rnrr_cull_right;", lines[right])
        patched += 1
    if patched:
        text = "\n".join(lines)
        if INCLUDE not in text:
            text = text.replace('#include "cpu_trace.h"\n', '#include "cpu_trace.h"\n' + INCLUDE, 1)
        path.write_text(text)
    return patched


OC_OLD = "? 6 : 8)"
OC_NEW = "? g_rnrr_oc_fast : g_rnrr_oc_slow)"


def patch_overclock(path: pathlib.Path) -> int:
    text = path.read_text()
    out = []
    n = 0
    for line in text.split("\n"):
        if "master_cycles +=" in line and OC_OLD in line:
            line = line.replace(OC_OLD, OC_NEW)
            n += 1
        out.append(line)
    if n:
        text = "\n".join(out)
        if INCLUDE not in text:
            text = text.replace('#include "cpu_trace.h"\n', '#include "cpu_trace.h"\n' + INCLUDE, 1)
        path.write_text(text)
    return n


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gen-dir", required=True)
    args = parser.parse_args()
    total = 0
    for path in sorted(pathlib.Path(args.gen_dir).glob("bank01*.c")):
        total += patch(path)
    print(f"apply_overrides: widened {total} sprite cull site(s)")
    oc = 0
    for path in sorted(pathlib.Path(args.gen_dir).glob("*.c")):
        oc += patch_overclock(path)
    print(f"apply_overrides: overclockable cycle charge site(s): {oc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
