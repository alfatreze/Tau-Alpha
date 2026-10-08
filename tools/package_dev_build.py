#!/usr/bin/env python3
"""Package a side-by-side developer test core from the Diagnostic Build.

Reuses the CURRENT dist/ bitstream unless --rbf is given (then --rbf-sha256 is required: an unaudited
RBF is refused). Library data slot 5 and cold-image data slot 6 are always added (the Diagnostic Build
needs both). Name the core with --semver (a feature milestone) or --number (a throwaway iteration).

  python3 tools/package_dev_build.py --semver 0.5.0-alpha.2
  python3 tools/package_dev_build.py --number 52 --variant diagnostic
  python3 tools/package_dev_build.py --release-diagnostic --rbf R --rbf-sha256 H   # what make_release.py runs
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tau_data_slots as slots_lib
import tau_layout

root = Path(__file__).resolve().parent.parent
src = root / "dist"

VARIANTS = {   # fw/build.sh target -> (ROM dir, kind text, default note)
    "profile":    ("library-diagnostic-profile", "diagnostic build with the media library plus decoder profiling",
                   "per-stage MP3/FLAC decode cost in the Check QR record"),
    "diagnostic": ("library-diagnostic", "diagnostic build with the media library", "Check, Tests and Stress pages"),
    # B-558: the release-style firmware (settings menu, Info page, no Check) built for the 192 KB link with the Cymo tempo funnel (TEMPO=1): fw/build.sh release
    # writes to work/ram192k/release when RAM_192K=1, so dist/ is never touched. Needs --build-flags with RAM_192K=1.
    "tempo":      ("release", "release-style build with Cymo tempo for MP3 (Settings > Playback > TEMPO)", "pitch-preserving MP3 speed, release-style",
                   "release", "work/ram192k"),
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
    ap.add_argument("--barcode", type=int, help="name the core 'TAU DEV BARCODE NN' (alfatreze.TAU_DEV_BARCODE_NN): test builds of the barcode-study branch "
                                                "(keeps its numbering apart from TAU DEV NN and TAU DEV METER NN)")
    ap.add_argument("--semver", help="name the core after the release it works toward, e.g. 0.5.0-alpha.1 "
                                     "(X.Y.Z-tag.N, tag alpha/beta/rc); use for real feature milestones")
    ap.add_argument("--release-diagnostic", action="store_true",
                    help="the shipped Diagnostic Build (alfatreze.TAU_DIAGNOSTIC, used by make_release.py): "
                         "variant diagnostic, needs --rbf/--rbf-sha256, no --number/--semver")
    ap.add_argument("--cover-slot", action="store_true", help="(kept for old command lines; data slot 7, the TIM1 cover image, is always declared now)")
    ap.add_argument("--note", help="replaces the default text after the build kind in the description")
    ap.add_argument("--rbf", type=Path, help="pair the ROM with this raw Quartus RBF instead of dist/'s")
    ap.add_argument("--rbf-sha256", help="expected SHA-256 of --rbf (required with --rbf)")
    ap.add_argument("--build-flags",
                    help="B-448: build fw/build.sh's player-<variant> target ourselves, right here, right "
                         "before packaging, with these env flags (comma-separated KEY=VAL, e.g. "
                         "'RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1') -- closes a real gap where a "
                         "manually-run flagged build and a LATER unflagged rebuild of the exact same "
                         "target (e.g. tools/check_heap_gap.py, which rebuilds every tracked target with "
                         "no flags of its own) land at the identical work/diagnostics/<romdir>/tau.rom "
                         "path, so running any check script between 'build the real firmware' and "
                         "'package it' can silently swap in the wrong variant -- same reported sizes, "
                         "genuinely different bytes, no error anywhere. A_36 shipped exactly that: an "
                         "unflagged ROM paired with a bitstream built for TAU_RAM_192K/TAU_CLK66/"
                         "TAU_SDRAM_BUSY/TAU_LPC, which just showed as an immediate black screen on real "
                         "hardware with no diagnostic signal at all. Passing this makes the build the "
                         "literal last step before the packaging that follows it, in the same process, "
                         "so nothing else can run in between. Omit it to keep using whatever is already "
                         "sitting at that path (the previous behaviour, still fine for make_release.py's "
                         "own --release-diagnostic, which never uses these flags).")
    args = ap.parse_args()
    if args.release_diagnostic:
        if args.number is not None or args.semver or args.barcode is not None or not args.rbf:
            sys.exit("--release-diagnostic takes --rbf/--rbf-sha256 and no --number/--semver/--barcode")
        args.variant = "diagnostic"
    elif [args.number is not None, bool(args.semver), args.barcode is not None].count(True) != 1:
        sys.exit("give exactly one of --number, --semver or --barcode")

    romdir, kind, default_note, *extra = VARIANTS[args.variant]
    target = extra[0] if extra else f"player-{romdir}"
    base = extra[1] if len(extra) > 1 else "work/diagnostics"
    rom = root / base / romdir / "tau.rom"
    if args.variant == "tempo" and (args.build_flags is None or "RAM_192K=1" not in args.build_flags):
        sys.exit("--variant tempo needs --build-flags including RAM_192K=1 (otherwise fw/build.sh release would overwrite dist/)")
    if args.build_flags is not None:
        env = dict(os.environ)
        for kv in args.build_flags.split(","):
            kv = kv.strip()
            if not kv:
                continue
            if "=" not in kv:
                sys.exit(f"--build-flags: '{kv}' is not KEY=VAL")
            k, v = kv.split("=", 1)
            env[k.strip()] = v.strip()
        if args.variant == "tempo":
            env.setdefault("TEMPO", "1")
        print(f"building {target} ({args.build_flags}) ...", file=sys.stderr)
        r = subprocess.run(["bash", "fw/build.sh", target], cwd=root, env=env)
        if r.returncode != 0:
            sys.exit(f"build failed ({target}, {args.build_flags})")
    if not rom.exists():
        sys.exit(f"{rom} missing -- build it first (bash fw/build.sh {target})")
    if args.rbf:
        if not args.rbf_sha256:
            sys.exit("--rbf also needs --rbf-sha256 (refusing an unaudited RBF)")
        rbf = args.rbf if args.rbf.is_absolute() else root / args.rbf
        # 2026-09-28: --rbf bit-reverses whatever it's given (see bitrev() below), assuming a RAW
        # Quartus .rbf. Pointing it at an already-reversed file (e.g. another installed core's own
        # Cores/.../bitstream.rbf_r, copied off the card to reuse its bitstream) silently reverses it
        # a SECOND time, producing a corrupted configuration file that looks like a normal file (right
        # size, passes its own --rbf-sha256 check on the INPUT) but makes the FPGA fail to configure --
        # on a real Pocket this showed as "Error in framework RS:Bridge not responding" (the fabric
        # never comes up, so the bridge never answers), not a build-time or packaging-time error.
        # Every raw Quartus RBF this project has ever produced is named "ap_core.rbf" or similar, never
        # "*.rbf_r" -- that suffix is this project's own bitrev() output naming convention, so refusing
        # it here catches the real mistake without needing a "type" flag. To reuse an already-reversed
        # file (e.g. another core's exact bitstream), copy it directly over the packaged
        # Cores/<id>/bitstream.rbf_r afterwards instead of routing it through --rbf.
        if rbf.suffix == ".rbf_r" or rbf.name.endswith("_r"):
            sys.exit(f"{rbf} looks already bit-reversed (name ends .rbf_r) -- --rbf wants the RAW "
                      "Quartus .rbf and will reverse it again, corrupting it. Copy an already-reversed "
                      "file directly over the packaged bitstream.rbf_r instead, or point --rbf at the "
                      "raw .rbf this one was made from.")
        if digest(rbf) != args.rbf_sha256:
            sys.exit(f"RBF hash mismatch: expected {args.rbf_sha256}, got {digest(rbf)}")
        already_reversed = False
    else:
        rbf, already_reversed = src / "Cores/alfatreze.TAU/bitstream.rbf_r", True

    if args.release_diagnostic:
        platform, core_id, short, title = "tau_diagnostic", "alfatreze.TAU_DIAGNOSTIC", "TAU_DIAGNOSTIC", "TAU Diagnostic Build"
        desc, out = "TAU developer build: settings, Info page and diagnostic tests", root / "work/diagnostics/library-diagnostic/pocket"
    elif args.barcode is not None:
        # Dev channel (B-673, option a): one TAU Dev platform, TAU's media read in place, listed under TAU too. Shortnames may hold
        # spaces (B-673), so the Pocket shows "TAU DEV BARCODE 05" as written.
        nn = f"{args.barcode:02d}"
        platform, short, label = tau_layout.DEV_PLATFORM, f"TAU DEV BARCODE {nn}", f"barcode-study test build {nn}"
        core_id, title = f"alfatreze.{short}", tau_layout.DEV_PLATFORM_NAME
        out = root / f"work/diagnostics/tau-dev-barcode-{nn}/pocket"
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
        platform, short, label = tau_layout.DEV_PLATFORM, f"TAU DEV {nn}", f"numbered test build {nn}"
        core_id, title = f"alfatreze.{short}", tau_layout.DEV_PLATFORM_NAME
        out = root / f"work/diagnostics/tau-dev-{nn}/pocket"
    dev_channel = platform == tau_layout.DEV_PLATFORM
    if not args.release_diagnostic:
        desc = f"TAU {label}: {kind}, " + (args.note or default_note)

    if out.exists(): shutil.rmtree(out)
    c = out / "Cores" / core_id
    shutil.copytree(src / "Cores/alfatreze.TAU", c)
    if already_reversed: shutil.copy2(rbf, c / "bitstream.rbf_r")
    else: bitrev(rbf, c / "bitstream.rbf_r")
    j = json.loads((c / "core.json").read_text())
    m = j["core"]["metadata"]; m["shortname"] = short
    m["platform_ids"] = [platform, tau_layout.MEDIA_PLATFORM] if dev_channel else [platform]
    # core.json description is limited to 63 characters (cores vanish from the menu otherwise, B-142);
    # the full text goes to info.txt, the About-screen field that is meant for it.
    if len(desc) > 63:
        (c / "info.txt").write_text(desc + "\n")
        desc = desc[:60] + "..."
    m["description"] = desc
    save(c / "core.json", j)
    slots_lib.add_library_slot(c)                       # data slot 5 + persist words 24-26 (27 retired)
    if len(platform) > 15 or not re.fullmatch(r"[a-z0-9][a-z0-9_]*", platform):
        raise ValueError(f"invalid Analogue Pocket platform shortname: {platform!r}")
    if len(short) > 31: raise ValueError(f"core shortname exceeds Pocket limit: {short!r}")
    if c.name != f"{m['author']}.{short}": raise ValueError("core folder does not match author.shortname metadata")
    if not (rom.parent / "tau-cold.bin").exists(): sys.exit(f"{rom.parent}/tau-cold.bin missing")
    slots_lib.add_cold_slot(c)                          # data slot 6 = the cold image
    slots_lib.add_assets_slot(c)                        # data slot 8 = tau-assets.bin (extra themes; optional file)
    slots_lib.add_cover_slot(c)                         # data slot 7 = the cover image (TIM1 reader, on by default since B-325)
    if dev_channel:                                     # library index and tau-assets.bin come from TAU's common/ (platform_ids[1])
        save(c / "data.json", tau_layout.read_shared_media(json.loads((c / "data.json").read_text())))
    a = out / "Assets" / platform
    cd = a / core_id                                   # H4: build-bound files are core-specific (tools/tau_layout.py)
    cd.mkdir(parents=True)
    shutil.copy2(rom, cd / "tau.rom")
    shutil.copy2(rom.parent / "tau-cold.bin", cd / "tau-cold.bin")
    shutil.copy2(src / "Assets/tau/alfatreze.TAU/tau-loading.bin", cd / "tau-loading.bin")
    # No Assets/<platform>/<core>/<title>.json any more (RELEASE_SYSTEM_SPEC section 11): no data slot has the instance bit, and it used
    # `variant_select`, a key the instance schema does not have, so the Pocket never read it. Older cards: listed as obsolete in tools/omega_compat.json.
    p = out / "Platforms"; (p / "_images").mkdir(parents=True)
    shutil.copy2(src / "Platforms/_images/tau.bin", p / "_images" / f"{platform}.bin")
    save(p / f"{platform}.json", {"platform": {"category": "Media Players", "name": title, "year": 2026, "manufacturer": "alfatreze"}})
    man = Path(str(rbf) + ".json") if args.rbf else None                    # B-653: what this bitstream was built with (tools/vm_fit.py collect)
    pair_cmd = [sys.executable, "tools/check_fw_bitstream_pair.py", str(out)]
    if man is not None and man.is_file():
        shutil.copy2(man, out / "bitstream-manifest.json")                    # stays in the package root: install_dev_core.py reads it, it is never copied to the card
        pair_cmd += ["--bitstream-manifest", str(out / "bitstream-manifest.json")]
    else:
        print("note: no bitstream manifest for this RBF; the firmware/bitstream FEATURE check is skipped (the version check still runs)", file=sys.stderr)
    r = subprocess.run(pair_cmd, cwd=root)   # B-581
    if r.returncode != 0:
        sys.exit("firmware/bitstream pairing check failed (see above); package left in " + str(out) + " but do not install it")
    # Review M5: a dev package carries its own release manifest next to it (tau-compat.json + a deterministic zip), so the installer
    # and Tau Omega's local-package flow get the same layout and pairing facts as a GitHub release. Needs the fit manifest for the
    # bitstream's CORE_VERSION and features; without one it is skipped with a note (dev builds on dist/'s bitstream).
    if not args.release_diagnostic:
        sys.path.insert(0, str(root / "tools"))
        import tau_compat
        label = args.semver or (f"barcode.{args.barcode}" if args.barcode is not None else args.number)
        if man is not None and man.is_file():
            try:
                cp = tau_compat.build_dev(out, label, rbf=rbf)
                print(f"manifest {cp} ({json.loads(cp.read_text())['release']})")
            except tau_compat.CompatError as e:
                sys.exit(f"tau-compat.json for this dev package failed: {e}")
        else:
            print("note: no fit manifest for this RBF: no tau-compat.json for this dev package (the installer skips its card check)", file=sys.stderr)
    print(core_id, digest(c / "bitstream.rbf_r"), digest(cd / "tau.rom"))

if __name__ == "__main__":
    main()
