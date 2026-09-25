#!/usr/bin/env python3
"""Build a Tau release: the normal core and the Diagnostic Build, each in its own zip.

Both cores run on the same FPGA bitstream and the same firmware source; the Diagnostic Build only has the test
menus switched on. Zip names follow Analogue's convention <Author>.<Core>_<Version>_<Date>.zip and contain only
Pocket base folders (Cores, Platforms, Assets). Version and date come from dist/Cores/alfatreze.TAU/core.json.

  python3 tools/make_release.py --rbf PATH_TO_RAW.rbf --rbf-sha256 HASH [--test]

Steps: build both ROMs, package the normal core (package.py), package the diagnostic core
(tools/package_dev_build.py --release-diagnostic), check both, write release/<name>.zip x2 and release/SHA256SUMS.txt.
Bump the version first (fw/player.c APP_VER, core.json, README 'Current version', CHANGELOG); build.sh refuses a mismatch.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIAG = ROOT / "work/diagnostics/library-diagnostic/pocket"   # B-078: the Diagnostic Build now includes the media library and Check
OUT = ROOT / "release"


def sh(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    r = subprocess.run(cmd, cwd=ROOT, **kw)
    if r.returncode:
        sys.exit(f"step failed ({r.returncode}): {' '.join(str(c) for c in cmd)}")


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def make_zip(src: Path, name: str):
    z = OUT / name
    if z.exists():
        z.unlink()
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as f:
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for n in sorted(files):
                if n.startswith("._") or n == ".DS_Store":
                    continue
                p = Path(root) / n
                zi = zipfile.ZipInfo(str(p.relative_to(src)), (2026, 1, 1, 0, 0, 0))
                zi.compress_type = zipfile.ZIP_DEFLATED
                zi.external_attr = 0o644 << 16
                f.writestr(zi, p.read_bytes())
    return z


def verify(z: Path, core_id: str, version: str, raw_rbf: bytes, rom: Path):
    rev = raw_rbf.translate(bytes(int(f"{x:08b}"[::-1], 2) for x in range(256)))
    with zipfile.ZipFile(z) as f:
        names = f.namelist()
        tops = {n.split("/")[0] for n in names}
        assert tops <= {"Cores", "Platforms", "Assets"}, f"unexpected base folders {tops}"
        assert not any("/._" in "/" + n or n.endswith(".DS_Store") for n in names), "metadata files in zip"
        core = json.loads(f.read(f"Cores/{core_id}/core.json"))["core"]["metadata"]
        assert f"{core['author']}.{core['shortname']}" == core_id, "core folder does not match author.shortname"
        assert core["version"] == version, f"core.json version {core['version']} != {version}"
        assert f.read(f"Cores/{core_id}/bitstream.rbf_r") == rev, "bitstream is not the bit-reversed RBF"
        roms = [n for n in names if n.endswith("common/tau.rom")]
        assert len(roms) == 1 and f.read(roms[0]) == rom.read_bytes(), "tau.rom is not the freshly built ROM"
        plat = core["platform_ids"][0]
        assert len(plat) <= 15 and f"Platforms/{plat}.json" in names and f"Platforms/_images/{plat}.bin" in names
        return len(names)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rbf", required=True, type=Path, help="raw (not reversed) RBF for both cores")
    ap.add_argument("--rbf-sha256", required=True, help="expected SHA-256 of --rbf (refuses an unaudited bitstream)")
    ap.add_argument("--test", action="store_true", help="also run make test-host")
    args = ap.parse_args()
    rbf = args.rbf if args.rbf.is_absolute() else ROOT / args.rbf
    if sha(rbf) != args.rbf_sha256:
        sys.exit(f"RBF hash mismatch: expected {args.rbf_sha256}, got {sha(rbf)}")
    raw = rbf.read_bytes()

    meta = json.loads((ROOT / "dist/Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]
    version, date = meta["version"], meta["date_release"]
    print(f"release v{version} ({date})")

    env = dict(os.environ)
    sh(["bash", "-n", "fw/build.sh"])
    sh(["bash", "fw/build.sh", "release"], env=env)                       # v0.4.0 (B-078): library + Phase G, no Check
    sh(["bash", "fw/build.sh", "player-library-diagnostic"], env=env)     # Diagnostic Build: adds Tests/Stress and the Check
    sh([sys.executable, "package.py", "--rbf", str(rbf), "--rbf-sha256", args.rbf_sha256, "--release-library"])
    sh([sys.executable, "tools/check_tau_package.py"])
    sh([sys.executable, "tools/package_dev_build.py", "--release-diagnostic",
        "--rbf", str(rbf), "--rbf-sha256", args.rbf_sha256])
    if args.test:
        sh(["make", "test-host"])

    OUT.mkdir(exist_ok=True)
    jobs = [
        ("alfatreze.TAU", ROOT / "dist", ROOT / "dist/Assets/tau/common/tau.rom"),
        ("alfatreze.TAU_DIAGNOSTIC", DIAG, DIAG / "Assets/tau_diagnostic/common/tau.rom"),
    ]
    sums = []
    for core_id, src, rom in jobs:
        z = make_zip(src, f"{core_id}_{version}_{date}.zip")
        n = verify(z, core_id, version, raw, rom)
        sums.append(f"{sha(z)}  {z.name}")
        print(f"  {z.name}: {n} files, {z.stat().st_size:,} bytes, checks ok")
    (OUT / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n")
    print("\n".join(sums))


if __name__ == "__main__":
    main()
