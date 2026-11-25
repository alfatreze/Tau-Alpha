#!/usr/bin/env python3
"""Run ONE map-only (quartus_map) configuration of the Talos engine on the Quartus VM. MIT licence.

  run_probe.py NAME [--engine PATH] [--param K=V ...] [--macro K[=V] ...] [--extra FILE ...] [--dry-run]

Stage: ~/tau-local/probe-talos-feat-NAME on the VM (flat: talos_feat.qsf/.qpf, tau_talos.sv = --engine, the leaf modules).
Runs `quartus_map talos_feat` (analysis and synthesis ONLY: no fitter, no full compile), waits for it, measures the wall
time, pulls the report files to work/talos_feat/NAME/ and prints the resource summary. Refuses to start while another
quartus_fit / quartus_asm / quartus_sta is running, or the load is 4 or more, or free memory is under 4 GB (the VM is
shared); a running quartus_map of another project is reported, not killed.
Same ssh constants as tools/vm_fit.py / tools/sdram_probe.py. Touches only probe-talos-feat-* directories.
"""
import argparse, io, re, subprocess, sys, tarfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
KEY = Path.home() / ".ssh/taualpha_vm_ed25519"
SSH = ["ssh", "-i", str(KEY), "-p", "2222", "-o", "ConnectTimeout=15", "-o", "BatchMode=yes", "taualpha@127.0.0.1"]
QMAP = "/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_map"
BASE = "tau-local"
LEAVES = ["src/fpga/core/tau_cdc_gray_ctr.sv", "src/fpga/core/font_rom.v", "src/fpga/core/font_ram.v"]
OUT = ROOT / "work/talos_feat"


def ssh(cmd, inp=None, check=True):
    p = subprocess.run(SSH + [cmd], input=inp, capture_output=True)
    if check and p.returncode:
        sys.exit(f"ssh failed ({p.returncode}): {p.stderr.decode()[-300:]}")
    return p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--engine", default=str(ROOT / "src/fpga/core/tau_talos.sv"))
    ap.add_argument("--param", action="append", default=[])
    ap.add_argument("--macro", action="append", default=[])
    ap.add_argument("--extra", action="append", default=[], help="more source files staged next to the engine and added to the QSF")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", a.name):
        sys.exit("bad name")
    d = f"{BASE}/probe-talos-feat-{a.name}"
    extra = ""
    for p in a.param:
        k, v = p.split("=")
        extra += f"set_parameter -name {k} {v}\n"
    for m in a.macro:
        extra += f'set_global_assignment -name VERILOG_MACRO "{m if "=" in m else m + "=1"}"\n'
    files = {"talos_feat.qpf": (HERE / "talos_feat.qpf").read_bytes(),
             "talos_feat.qsf": (HERE / "talos_feat.qsf").read_bytes() + b"\n# --- per configuration ---\n"
             + "".join(f"set_global_assignment -name SYSTEMVERILOG_FILE {Path(e).name}\n" for e in a.extra).encode()
             + extra.encode(),
             "tau_talos.sv": Path(a.engine).read_bytes()}
    for f in LEAVES:
        files[Path(f).name] = (ROOT / f).read_bytes()
    for e in a.extra:
        files[Path(e).name] = Path(e).read_bytes()
    print(f"stage {d}: {len(files)} files; engine {a.engine}\n--- appended to the QSF ---\n{extra or '(nothing: all defaults)'}")
    if a.dry_run:
        return
    st = ssh("uptime; pgrep -a quartus_; free -m | sed -n 2p; test -e %s && echo EXISTS" % d, check=False).decode()
    print(st)
    if "EXISTS" in st:
        sys.exit("stage exists; pick another name")
    load = float(re.search(r"load average: (\d+[.,]\d+)", st).group(1).replace(",", "."))
    avail = int(st.split("Mem:")[1].split()[5])
    if load >= 4 or avail < 4000:
        sys.exit(f"VM too busy (load {load}, available {avail} MB)")
    heavy = len(re.findall(r"quartus_(?:fit|asm|sta|eda)\b", st))
    if heavy >= 2:
        sys.exit(f"{heavy} heavy Quartus jobs of other projects are running; wait")
    if heavy:
        print(f"note: {heavy} other fit/sta job is running; one single-entity quartus_map is light, load is {load}")
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for n, b in files.items():
            ti = tarfile.TarInfo(n); ti.size = len(b); ti.mtime = int(time.time()); t.addfile(ti, io.BytesIO(b))
    ssh(f"mkdir -p {d} && tar xzf - -C {d}", inp=buf.getvalue())
    t0 = time.time()
    print("launched", time.strftime("%H:%M:%S"))
    ssh(f"cd {d} && (time {QMAP} talos_feat) > quartus-map.log 2>&1; touch map.done")
    secs = time.time() - t0
    print(f"quartus_map wall time {secs:.0f} s")
    o = OUT / a.name
    o.mkdir(parents=True, exist_ok=True)
    tarb = ssh(f"cd {d} && tar czf - quartus-map.log output_files/*.map.rpt output_files/*.map.summary")
    with tarfile.open(fileobj=io.BytesIO(tarb), mode="r:gz") as t:
        t.extractall(o)
    (o / "wall_seconds.txt").write_text(f"{secs:.0f}\n")
    print((o / "output_files/talos_feat.map.summary").read_text())


if __name__ == "__main__":
    main()
