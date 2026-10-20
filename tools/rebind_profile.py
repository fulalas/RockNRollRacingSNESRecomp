"""Rebind a tier2 coverage checkpoint to the digest of a patched ROM image."""
import hashlib
import json
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: rebind_profile.py <in.json> <out.json> <rom>")
rom = open(sys.argv[3], "rb").read()
if len(rom) % 1024 == 512:
    rom = rom[512:]
data = json.load(open(sys.argv[1]))
data["identity"]["rom_sha256"] = hashlib.sha256(rom).hexdigest()
json.dump(data, open(sys.argv[2], "w"))
