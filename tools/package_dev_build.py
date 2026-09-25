#!/usr/bin/env python3
"""Package a side-by-side developer test core from the Diagnostic Build.

Reuses the CURRENT dist/ bitstream unless --rbf is given (then --rbf-sha256 is required: an unaudited
RBF is refused). Library data slot 5 and cold-image data slot 6 are always added (the Diagnostic Build
needs both). Name the core with --semver (a feature milestone) or --number (a throwaway iteration).

  python3 tools/package_dev_build.py --semver 0.5.0-alpha.2
  python3 tools/package_dev_build.py --number 52 --variant diagnostic
  python3 tools/package_dev_build.py --release-diagnostic --rbf R --rbf-sha256 H   # what make_release.py runs
"""
import argparse, hashlib, json, re, shutil, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tau_data_slots as slots_lib

root = Path(__file__).resolve().parent.parent
src = root / "dist"

VARIANTS = {   # fw/build.sh target -> (ROM dir, kind text, default note)
    "profile":    ("library-diagnostic-profile", "diagnostic build with the media library plus decoder profiling",
                   "per-stage MP3/FLAC decode cost in the Check QR record"),
    "diagnostic": ("library-diagnostic", "diagnostic build with the media library", "Check, Tests and Stress pages"),
}

def save(p, v): p.write_text(json.dumps(v, indent=4) + "\n")
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def bitrev(a, b): b.write_bytes(a.read_bytes().translate(bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--variant", choices=VARIANTS, default="profile",
                    help="profile = fw/build.sh player-library-diagnostic-profile (default); "
                         "diagnostic = player-library-diagnostic")
    ap.add_argument("--number", type=int, help="name the core 'TAU DEV NN' (throwaway iteration)")
    ap.add_argument("--semver", help="name the core after the release it works toward, e.g. 0.5.0-alpha.1 "
                                     "(X.Y.Z-tag.N, tag alpha/beta/rc); use for real feature milestones")
    ap.add_argument("--release-diagnostic", action="store_true",
                    help="the shipped Diagnostic Build (alfatreze.TAU_DIAGNOSTIC, used by make_release.py): "
                         "variant diagnostic, needs --rbf/--rbf-sha256, no --number/--semver")
    ap.add_argument("--note", help="replaces the default text after the build kind in the description")
    ap.add_argument("--rbf", type=Path, help="pair the ROM with this raw Quartus RBF instead of dist/'s")
    ap.add_argument("--rbf-sha256", help="expected SHA-256 of --rbf (required with --rbf)")
    args = ap.parse_args()
    if args.release_diagnostic:
        if args.number is not None or args.semver or not args.rbf:
            sys.exit("--release-diagnostic takes --rbf/--rbf-sha256 and no --number/--semver")
        args.variant = "diagnostic"
    elif (args.number is None) == (not args.semver):
        sys.exit("give exactly one of --number or --semver")

    romdir, kind, default_note = VARIANTS[args.variant]
    rom = root / "work/diagnostics" / romdir / "tau.rom"
    if not rom.exists():
        sys.exit(f"{rom} missing -- build it first (bash fw/build.sh player-{romdir})")
    if args.rbf:
        if not args.rbf_sha256:
            sys.exit("--rbf also needs --rbf-sha256 (refusing an unaudited RBF)")
        rbf = args.rbf if args.rbf.is_absolute() else root / args.rbf
        if digest(rbf) != args.rbf_sha256:
            sys.exit(f"RBF hash mismatch: expected {args.rbf_sha256}, got {digest(rbf)}")
        already_reversed = False
    else:
        rbf, already_reversed = src / "Cores/alfatreze.TAU/bitstream.rbf_r", True

    if args.release_diagnostic:
        platform, core_id, short, title = "tau_diagnostic", "alfatreze.TAU_DIAGNOSTIC", "TAU_DIAGNOSTIC", "TAU Diagnostic Build"
        desc, out = "TAU developer build: settings, Info page and diagnostic tests", root / "work/diagnostics/library-diagnostic/pocket"
    elif args.semver:
        # Pocket platform ids match [a-z0-9][a-z0-9_]* and are <= 15 chars, so the semver is sanitized there;
        # the human-facing shortname/title/description keep the real string.
        if not re.fullmatch(r"\d+\.\d+\.\d+-(alpha|beta|rc)\.\d+", args.semver):
            sys.exit(f"--semver must look like '0.5.0-alpha.1': got {args.semver!r}")
        sid = re.sub(r"[.\-]", "_", args.semver).replace("alpha", "a").replace("beta", "b")
        platform, core_id = f"tau_{sid}", f"alfatreze.TAU_{sid.upper()}"
        short, title = f"TAU_{sid.upper()}", f"TAU {args.semver}"
        label, out = args.semver, root / f"work/diagnostics/tau-{sid}/pocket"
    else:
        nn = f"{args.number:02d}"
        platform, core_id = f"tau_dev_{nn}", f"alfatreze.TAU_DEV_{nn}"
        short, title, label = f"TAU_DEV_{nn}", f"TAU DEV {nn}", f"numbered test build {nn}"
        out = root / f"work/diagnostics/tau-dev-{nn}/pocket"
    if not args.release_diagnostic:
        desc = f"TAU {label}: {kind}, " + (args.note or default_note)

    if out.exists(): shutil.rmtree(out)
    c = out / "Cores" / core_id
    shutil.copytree(src / "Cores/alfatreze.TAU", c)
    if already_reversed: shutil.copy2(rbf, c / "bitstream.rbf_r")
    else: bitrev(rbf, c / "bitstream.rbf_r")
    j = json.loads((c / "core.json").read_text())
    m = j["core"]["metadata"]; m["shortname"] = short; m["platform_ids"] = [platform]
    # core.json description is limited to 63 characters (cores vanish from the menu otherwise, B-142);
    # the full text goes to info.txt, the About-screen field that is meant for it.
    if len(desc) > 63:
        (c / "info.txt").write_text(desc + "\n")
        desc = desc[:60] + "..."
    m["description"] = desc
    save(c / "core.json", j)
    slots_lib.add_library_slot(c)                       # data slot 5 + persist words 24-27
    if len(platform) > 15 or not re.fullmatch(r"[a-z0-9][a-z0-9_]*", platform):
        raise ValueError(f"invalid Analogue Pocket platform shortname: {platform!r}")
    if len(short) > 31: raise ValueError(f"core shortname exceeds Pocket limit: {short!r}")
    if c.name != f"{m['author']}.{short}": raise ValueError("core folder does not match author.shortname metadata")
    if not (rom.parent / "tau-cold.bin").exists(): sys.exit(f"{rom.parent}/tau-cold.bin missing")
    slots_lib.add_cold_slot(c)                          # data slot 6 = the cold image
    a = out / "Assets" / platform
    (a / "common").mkdir(parents=True); (a / core_id).mkdir()
    shutil.copy2(rom, a / "common/tau.rom")
    shutil.copy2(rom.parent / "tau-cold.bin", a / "common/tau-cold.bin")
    shutil.copy2(src / "Assets/tau/common/tau-loading.bin", a / "common/tau-loading.bin")
    save(a / core_id / f"{title}.json", {"instance": {"magic": "APF_VER_1", "variant_select": {"id": 0, "select": False},
         "data_path": "", "data_slots": [{"id": 1, "filename": "tau.rom"}], "memory_writes": []}})
    p = out / "Platforms"; (p / "_images").mkdir(parents=True)
    shutil.copy2(src / "Platforms/_images/tau.bin", p / "_images" / f"{platform}.bin")
    save(p / f"{platform}.json", {"platform": {"category": "Media Players", "name": title, "year": 2026, "manufacturer": "alfatreze"}})
    print(core_id, digest(c / "bitstream.rbf_r"), digest(a / "common/tau.rom"))

if __name__ == "__main__":
    main()
