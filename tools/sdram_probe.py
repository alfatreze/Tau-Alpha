#!/usr/bin/env python3
"""Stage, launch, check and collect the synthesis + fit probe of the SDRAM PHY + scheduler (contract 7.5) on the build VM.

  python3 tools/sdram_probe.py launch NAME [--variant base|cal|bist|perf|all|cal,bist] [--seed 1] [--dry-run]
  python3 tools/sdram_probe.py status NAME      # running / done; ALMs, M10K, registers, Fmax and the worst setup/hold slack per corner
  python3 tools/sdram_probe.py collect NAME     # copy the reports to work/diagnostics/sdram_probe-NAME/ (status does it when the run is done)

Same shape as tools/vm_fit.py: the same ssh/scp constants, a stage directory ~/tau-local/sdram_probe-NAME on the VM, a
detached `quartus_sh --flow compile`, and a refusal to start while the VM has a Quartus process running (use --force
only if you are sure it is idle). The stage is FLAT: the probe's top, QSF, QPF, SDC and probe_report.tcl at its root,
the RTL it needs copied from the CURRENT working tree (uncommitted changes included) into rtl/. After the compile the
detached job runs `quartus_sta -t probe_report.tcl` (per-corner slack of the core, output and input paths) and touches
probe.done.

--dry-run prints every ssh/scp command and the file list and does nothing else (no connection to the VM).
If the VM address, key or Quartus path changes, change the constants below (they are those of vm_fit.py) and nothing else.
"""
import argparse, io, json, re, subprocess, sys, tarfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEY = Path.home() / ".ssh/taualpha_vm_ed25519"
SSH = ["ssh", "-i", str(KEY), "-p", "2222", "-o", "ConnectTimeout=10", "taualpha@127.0.0.1"]
SCP = ["scp", "-q", "-i", str(KEY), "-P", "2222"]
QBIN = "/home/taualpha/intelFPGA_lite/25.1std/quartus/bin"
QUARTUS = QBIN + "/quartus_sh"
QSTA = QBIN + "/quartus_sta"
BASE = "tau-local"
REV = "sdram_probe"
PROBE = ROOT / "tools/sdram_probe"
CORE = ROOT / "src/fpga/core"
LOCAL_OUT = ROOT / "work/diagnostics"

# stage path -> repo path. The QSF refers to exactly these.
STAGE_FILES = {
    "sdram_probe.qpf": PROBE / "sdram_probe.qpf",
    "sdram_probe.qsf": PROBE / "sdram_probe.qsf",
    "sdram_probe.sdc": PROBE / "sdram_probe.sdc",
    "tau_sdram_probe_top.sv": PROBE / "tau_sdram_probe_top.sv",
    "probe_report.tcl": PROBE / "probe_report.tcl",
    **{f"rtl/{n}": CORE / n for n in (
        "tau_sdram_phy.sv", "tau_sdram_phy_core.sv", "tau_sdram_dq_cap.sv", "tau_sdram_dq_cap_dual.sv",
        "tau_sdram_clk_out.sv", "tau_sdram_sched_v.sv", "tau_sdram_sv_pq.sv", "tau_sdram_sv_arb.sv",
        "mf_pllbase.v", "mf_pllbase.qip", "mf_pllbase/mf_pllbase_0002.v", "mf_pllbase/mf_pllbase_0002.qip")},
}
VARIANT_MACROS = {"cal": "PROBE_CALIBRATE", "bist": "PROBE_BIST", "perf": "PROBE_PERF"}
EXPECT_MIN = 15     # minutes (estimate, NOT measured: the full core takes 1h40; this design is a fraction of it)
EXPECT_MAX = 30

DRY = False


def stage_dir(name):
    return f"{BASE}/sdram_probe-{name}"


def ssh(cmd, check=True, inp=None):
    if DRY:
        print("  [dry-run] ssh:", cmd, "  (stdin: %d bytes)" % len(inp) if inp else "")
        return ""
    r = subprocess.run(SSH + [cmd], capture_output=True, text=True, input=inp)
    if check and r.returncode != 0:
        sys.exit(f"VM command failed ({r.returncode}): {cmd}\n{r.stdout}{r.stderr}")
    return r.stdout


def ssh_bytes(cmd, inp):
    if DRY:
        print(f"  [dry-run] ssh: {cmd}  (stdin: {len(inp)} bytes tar)")
        return
    p = subprocess.run(SSH + [cmd], input=inp, capture_output=True)
    if p.returncode != 0:
        sys.exit(f"stage failed: {p.stderr.decode()[-300:]}")


def running_compiles():
    out = ssh("pgrep -a quartus_sh; pgrep -a quartus_fit; pgrep -a quartus_map; pgrep -a quartus_sta", check=False)
    return [l for l in out.splitlines() if l.strip()]


def variant_macros(variant):
    names = [v.strip() for v in variant.split(",") if v.strip()]
    if names == ["all"]:
        names = list(VARIANT_MACROS)
    if names == ["base"]:
        names = []
    bad = [n for n in names if n not in VARIANT_MACROS]
    if bad:
        sys.exit(f"unknown variant {bad}; use base, cal, bist, perf, all or a comma list like cal,bist")
    return [VARIANT_MACROS[n] for n in names]


def check_macros(macros):
    """A macro nothing reads builds fine and measures something else (the lesson of fit_manifest.dead_macros)."""
    text = "".join(p.read_text() for p in STAGE_FILES.values() if p.suffix in (".sv", ".v"))
    for m in macros + ["TAU_SDRAM_ALTDDIO_OUT", "TAU_SDRAM_ALTDDIO_IN"]:
        if not re.search(r"`ifn?def\s+" + m + r"\b", text):
            sys.exit(f"macro {m} is read by no staged RTL; fix the probe before launching")


def stage_tar():
    missing = [str(p) for p in STAGE_FILES.values() if not p.is_file()]
    if missing:
        sys.exit("missing source files: " + ", ".join(missing))
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for arc, p in STAGE_FILES.items():
            t.add(p, arcname=arc)
    return buf.getvalue()


def cmd_launch(a):
    macros = variant_macros(a.variant)
    check_macros(macros)
    d = stage_dir(a.name)
    busy = running_compiles()
    if busy and not a.force:
        sys.exit("the VM already has a Quartus process running (use --force only if you are sure it is idle):\n" + "\n".join(busy))
    data = stage_tar()
    print(f"stage {d}: {len(STAGE_FILES)} files, {len(data) / 1e3:.0f} KB; variant macros: {macros or 'none (base: CALIBRATE, BIST, PERF off)'}")
    if DRY:
        for arc, p in STAGE_FILES.items():
            print(f"  {arc:40s} <- {p.relative_to(ROOT)}")
    if not DRY and ssh(f"test -e {d} && echo EXISTS", check=False).strip():
        sys.exit(f"{d} already exists on the VM; pick another NAME")
    ssh_bytes(f"mkdir -p {d} && tar xzf - -C {d}", data)
    extra = "\n# --- sdram_probe.py launch %s variant %s seed %d ---\n" % (a.name, a.variant, a.seed)
    extra += "".join(f'set_global_assignment -name VERILOG_MACRO "{m}=1"\n' for m in macros)
    extra += f"set_global_assignment -name SEED {a.seed}\n"
    ssh(f"cat >> {d}/{REV}.qsf", inp=extra)
    job = (f"{QUARTUS} --flow compile {REV} > quartus-fit.log 2>&1; "
           f"{QSTA} -t probe_report.tcl {REV} > probe_paths.txt 2>&1; touch probe.done")
    ssh(f"cd {d} && (setsid nohup sh -c '{job}' < /dev/null > /dev/null 2>&1 &) ; echo started")   # fully detached, or ssh never returns
    now = time.time()
    eta = lambda m: time.strftime("%H:%M", time.localtime(now + 60 * m))
    print(f"launched {d} at {time.strftime('%H:%M', time.localtime(now))}")
    print(f"expect it done around {eta(EXPECT_MIN)}-{eta(EXPECT_MAX)} local time (an estimate, not a measurement: the first probe run has no history; "
          f"the full core takes 1h40)")
    if not DRY:
        time.sleep(6)
        n = len([l for l in running_compiles() if "quartus_sh --flow compile" in l])
        print(f"quartus_sh compiles now running on the VM: {n} (expected 1)")
        if n != 1:
            sys.exit("STOP: unexpected number of compiles -- check the VM before trusting these results")
    if not DRY:
        LOCAL_OUT.joinpath(f"sdram_probe-{a.name}").mkdir(parents=True, exist_ok=True)
        (LOCAL_OUT / f"sdram_probe-{a.name}" / "launch.json").write_text(
            json.dumps({"name": a.name, "variant": a.variant, "macros": macros, "seed": a.seed, "launched": time.strftime("%Y-%m-%d %H:%M")}, indent=1) + "\n")


PULL = ("cd {d} && tar czf - quartus-fit.log probe_paths.txt probe_*.txt probe.done {o}/*.summary {o}/*.rpt "
        "2>/dev/null")


def state(d):
    """running / done / failed / not started."""
    if ssh(f"test -d {d} && echo Y", check=False).strip() != "Y":
        return "missing"
    if ssh(f"test -e {d}/probe.done && echo Y", check=False).strip() == "Y":
        log = ssh(f"cat {d}/quartus-fit.log", check=False)
        if re.search(r"Full Compilation was successful", log):
            return "done"
        return "failed"
    if any(REV in l or "quartus" in l for l in running_compiles()):
        return "running"
    return "unknown (no probe.done and no Quartus process)"


def cmd_collect(a, quiet=False):
    d = stage_dir(a.name)
    out = LOCAL_OUT / f"sdram_probe-{a.name}"
    out.mkdir(parents=True, exist_ok=True)
    if DRY:
        print(f"  [dry-run] ssh: {PULL.format(d=d, o='output_files')}  | tar xzf - -C {out}")
        return out
    p = subprocess.run(SSH + [PULL.format(d=d, o="output_files")], capture_output=True)
    if not p.stdout:
        sys.exit("nothing to collect (has the run started?)")
    with tarfile.open(fileobj=io.BytesIO(p.stdout), mode="r:gz") as t:
        t.extractall(out, filter="data") if sys.version_info >= (3, 12) else t.extractall(out)
    if not quiet:
        print(f"reports copied to {out}")
    return out


def table(lines, title_re):
    """First Quartus report table whose title matches: (header, rows)."""
    for i, line in enumerate(lines):
        m = re.match(r"^;\s*(" + title_re + r")\s*;?\s*$", line)
        if m and i and lines[i - 1].startswith("+"):
            j = i + 1
            while j < len(lines) and not (lines[j].startswith(";") and lines[j - 1].startswith("+")):
                j += 1
            header = [c.strip() for c in lines[j].strip().strip(";").split(";")]
            rows, k = [], j + 2
            while k < len(lines) and lines[k].startswith(";"):
                rows.append([c.strip() for c in lines[k].strip().strip(";").split(";")])
                k += 1
            return header, rows
    return None, []


def entity_rows(fit_rpt_text):
    """ALMs / registers / M10K of the probe top and of u_phy, u_sched from the fitter's resource table by entity."""
    lines = fit_rpt_text.split("\n")
    header, rows = table(lines, r"Fitter Resource Utilization by Entity")
    if not header:
        return ["   (no 'Fitter Resource Utilization by Entity' table found in the fit report)"]
    def col(sub):
        for i, h in enumerate(header):
            if sub in h:
                return i
        return None
    cols = [(lab, col(sub)) for lab, sub in (("ALMs", "ALMs needed"), ("regs", "Dedicated Logic Registers"),
                                             ("M10K", "M10K"), ("memory bits", "Block Memory Bits"))]
    out = []
    for r in rows:
        name = r[0].strip()
        if re.search(r"u_phy|u_sched|tau_sdram_probe_top", name) and name.count("|") <= 3 and ("u_phy" in name or "u_sched" in name or name.startswith("|tau_sdram_probe_top") or name.startswith("tau_sdram_probe_top")):
            vals = ", ".join(f"{lab} {r[i]}" for lab, i in cols if i is not None and i < len(r))
            out.append(f"   {name.strip('| ')[:48]:48s} {vals}")
    return out or ["   (entity table found but no u_phy / u_sched rows)"]


def fmax_rows(sta_rpt_text):
    lines = sta_rpt_text.split("\n")
    out = []
    for model in ("Slow 1100mV 85C", "Slow 1100mV 0C", "Fast 1100mV 85C", "Fast 1100mV 0C"):
        header, rows = table(lines, re.escape(model) + r" Model Fmax Summary")
        for r in rows:
            out.append(f"   {model} Fmax {r[0]} (restricted {r[1] if len(r) > 1 else '?'}) clock {r[2] if len(r) > 2 else '?'}")
    return out or ["   (no Fmax summary in sta.rpt)"]


def corner_slacks(sta_summary_text):
    best = {}
    for ty, sl in re.findall(r"Type\s*:\s*(.*?)\nSlack\s*:\s*(-?[\d.]+)", sta_summary_text):
        m = re.match(r"(Slow|Fast) 1100mV (\d+C) Model (Setup|Hold) '?(.*?)'?\s*$", ty.strip())
        if m:
            k = (m.group(1), m.group(2), m.group(3), m.group(4)[-60:])
            best[k] = min(best.get(k, 9e9), float(sl))
    return best


def cmd_status(a):
    d = stage_dir(a.name)
    if DRY:
        print("  [dry-run] would check:", f"test -e {d}/probe.done", "then collect and summarise")
        cmd_collect(a)
        return
    st = state(d)
    print(f"{d}: {st}")
    if st == "missing":
        sys.exit(f"no stage named {a.name} on the VM")
    log = ssh(f"tail -c 600 {d}/quartus-fit.log", check=False).strip().split("\n")
    print("   last log line:", log[-1][:150] if log and log[-1] else "(empty)")
    if st == "running":
        return
    out = cmd_collect(a, quiet=True)
    o = out / "output_files"
    summ = ROOT / "tools/quartus_fit_summary.py"
    r = subprocess.run([sys.executable, str(summ), str(o), "--log", str(out / "quartus-fit.log"), "--revision", REV, "--pin-prefix", "dram"],
                       capture_output=True, text=True)
    print(r.stdout.rstrip())
    fit_rpt = o / f"{REV}.fit.rpt"
    if fit_rpt.exists():
        print("   by entity (the wrapper's own registers are in the top row, not in u_phy / u_sched):")
        print("\n".join(entity_rows(fit_rpt.read_text(errors="replace"))))
    sta_rpt = o / f"{REV}.sta.rpt"
    if sta_rpt.exists():
        print("   Fmax (restricted, per corner):")
        print("\n".join(fmax_rows(sta_rpt.read_text(errors="replace"))))
    sta_sum = o / f"{REV}.sta.summary"
    if sta_sum.exists():
        print("   worst slack per corner and clock (Quartus summary; * = the two corners the contract asks for):")
        for (speed, temp, kind, clk), v in sorted(corner_slacks(sta_sum.read_text()).items()):
            star = "*" if (speed, temp) in (("Slow", "85C"), ("Fast", "0C")) else " "
            print(f"    {star} {speed} {temp} {kind:5s} {v:+.3f} ns  {clk}")
    paths = out / "probe_paths.txt"
    if paths.exists():
        print("   probe_report.tcl (core = clk->clk, out = clk->dram pins, in = dram_dq->capture register):")
        for l in paths.read_text(errors="replace").splitlines():
            if l.startswith(("RESULT", "FMAX", "GROUP")):
                print("    ", l)
    else:
        print("   probe_paths.txt missing: the post-fit timing report has not run (compile failed?)")
    print(f"   all reports: {out}")


def main():
    global DRY
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("launch")
    l.add_argument("name")
    l.add_argument("--variant", default="base", help="base (default), cal, bist, perf, all, or a comma list: which optional PHY features are on")
    l.add_argument("--seed", type=int, default=1)
    l.add_argument("--force", action="store_true")
    l.add_argument("--dry-run", action="store_true", help="print the commands and the file list, do not touch the VM")
    l.set_defaults(fn=cmd_launch)
    s = sub.add_parser("status"); s.add_argument("name"); s.add_argument("--dry-run", action="store_true"); s.set_defaults(fn=cmd_status)
    c = sub.add_parser("collect"); c.add_argument("name"); c.add_argument("--dry-run", action="store_true"); c.set_defaults(fn=cmd_collect)
    a = ap.parse_args()
    DRY = a.dry_run
    a.fn(a)


if __name__ == "__main__":
    main()
