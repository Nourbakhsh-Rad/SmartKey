#!/usr/bin/env python3
"""Fail if the version is not identical everywhere (run in CI and before a release)."""
import json, re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
found = {}

props = (root / "library.properties").read_text(encoding="utf-8")
found["library.properties"] = re.search(r"^version=(\S+)", props, re.M).group(1)

found["library.json"] = json.loads((root / "library.json").read_text(encoding="utf-8"))["version"]

core = (root / "src" / "SmartKeyCore.h").read_text(encoding="utf-8")
found["SMARTKEY_VERSION"] = re.search(r'#define SMARTKEY_VERSION "([^"]+)"', core).group(1)
major, minor, patch = (re.search(r"#define SMARTKEY_VERSION_%s (\d+)" % n, core).group(1)
                       for n in ("MAJOR", "MINOR", "PATCH"))
found["SMARTKEY_VERSION_*"] = "%s.%s.%s" % (major, minor, patch)

log = (root / "CHANGELOG.md").read_text(encoding="utf-8")
found["CHANGELOG.md (latest)"] = re.search(r"^## \[(\d+\.\d+\.\d+)\]", log, re.M).group(1)

readme = (root / "ReadMe.md").read_text(encoding="utf-8")
found["ReadMe.md badge"] = re.search(r"badge/version-(\d+\.\d+\.\d+)-", readme).group(1)

width = max(len(k) for k in found)
for k, v in found.items():
    print("%-*s  %s" % (width, k, v))

if len(set(found.values())) != 1:
    print("\nVersion mismatch!")
    sys.exit(1)
print("\nAll versions match.")
