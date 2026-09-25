#!/usr/bin/env python3
"""Stage, launch, check and collect Quartus fits on the build VM (the recurring hand-typed sequence, B-260).

  python3 tools/vm_fit.py launch NAME --append tools/blit_g3_vblank_qsf_append.txt --seed 1 --seed 2
  python3 tools/vm_fit.py status NAME            # every seed of NAME: running / done, resources, worst slack per corner
  python3 tools/vm_fit.py collect NAME --seed 1  # copy the RBF to work/diagnostics/<NAME>/ap_core.rbf and print its SHA-256

`launch` stages the CURRENT working tree of src/fpga (tracked files plus new untracked ones, so uncommitted RTL is
included -- that is the point), appends the macro/assignment file and a SEED line to each stage's ap_core.qsf, and
starts a detached `quartus_sh --flow compile` per seed. It refuses to start if the VM already has a Quartus
compile running, or if the stage name already exists. Stage dir: ~/tau-local/<NAME>-s<seed>.

If the VM address, key or Quartus path changes, change the constants below and nothing else.
"""
import argparse, hashlib, io, re, subprocess, sys, tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEY = Path.home() / ".ssh/taualpha_vm_ed25519"
SSH = ["ssh", "-i", str(KEY), "-p", "2222", "-o", "ConnectTimeout=10", "taualpha@127.0.0.1"]
SCP = ["scp", "-q", "-i", str(KEY), "-P", "2222"]
QUARTUS = "/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh"
BASE = "tau-local"


def ssh(cmd, check=True, inp=None):
    r = subprocess.run(SSH + [cmd], capture_output=True, text=True, input=inp)
    if check and r.returncode != 0:
        sys.exit(f"VM command failed ({r.returncode}): {cmd}\n{r.stdout}{r.stderr}")
    return r.stdout


def stage_dir(name, seed):
    return f"{BASE}/{name}-s{seed}"


def running_compiles():
    return [l for l in ssh("pgrep -a quartus_sh; pgrep -a quartus_fit; pgrep -a quartus_map", check=False).splitlines() if l.strip()]


def tree_tar():
    """Tarball of the working tree of src/fpga: tracked files (as they are on disk) plus untracked non-ignored ones."""
    files = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "src/fpga"],
                                    cwd=ROOT).decode().split("\0")
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for f in files:
            p = ROOT / f
            if f and p.is_file() and "/output_files/" not in f and "/db/" not in f and "/incremental_db/" not in f:
                t.add(p, arcname=f)
    return buf.getvalue()


def cmd_launch(a):
    if not a.seed:
        sys.exit("give at least one --seed")
    busy = running_compiles()
    if busy and not a.force:
        sys.exit("the VM already has a Quartus process running (use --force only if you are sure it is idle):\n" + "\n".join(busy))
    append = (ROOT / a.append).read_text() if a.append else ""
    data = tree_tar()
    print(f"staged tree: {len(data) / 1e6:.1f} MB")
    for seed in a.seed:
        d = stage_dir(a.name, seed)
        if ssh(f"test -e {d} && echo EXISTS", check=False).strip():
            sys.exit(f"{d} already exists on the VM; pick another NAME")
        p = subprocess.run(SSH + [f"mkdir -p {d} && tar xzf - -C {d}"], input=data, capture_output=True)
        if p.returncode != 0:
            sys.exit(f"stage failed: {p.stderr.decode()[-300:]}")
        qsf = f"{d}/src/fpga/ap_core.qsf"
        extra = f"\n# --- vm_fit.py launch {a.name} seed {seed} ---\n{append}\nset_global_assignment -name SEED {seed}\n"
        subprocess.run(SSH + [f"cat >> {qsf}"], input=extra.encode(), check=True)
        ssh(f"cd {d}/src/fpga && (setsid nohup {QUARTUS} --flow compile ap_core.qpf < /dev/null > ../../quartus-fit.log 2>&1 &) ; echo started")   # fully detached, or ssh never returns
        print(f"launched {d}")
    import time; time.sleep(6)
    n = len([l for l in running_compiles() if "quartus_sh --flow compile" in l])   # not the build-id helper
    print(f"quartus_sh processes now running on the VM: {n} (expected {len(a.seed)})")
    if n != len(a.seed):
        sys.exit("STOP: unexpected number of compiles -- check the VM before trusting these results")


STATUS_PY = r'''
import re,sys,os
d=sys.argv[1]; o=d+"/src/fpga/output_files/"
def rd(p):
    try: return open(p).read()
    except Exception: return ""
log=rd(d+"/quartus-fit.log")
fit=rd(o+"ap_core.fit.summary"); sta=rd(o+"ap_core.sta.summary")
run=os.popen("pgrep -f 'quartus_(sh|fit|map|asm|sta).*"+os.path.basename(d).split("-s")[0]+"' | wc -l").read().strip()
ok=re.search(r"Quartus Prime Full Compilation was successful. (\d+) errors",log)
err=re.search(r"Full Compilation was unsuccessful|Error \(",log)
state="DONE" if ok else ("FAILED" if err else "running/unknown")
print(d.split("/")[-1], state)
for k in ("Fitter Status","Total RAM Blocks","Total DSP Blocks","Logic utilization"):
    m=re.search(k+r"\s*:\s*(.*)",fit)
    if m: print("  ",k,":",m.group(1).strip())
best={}
for ty,sl in re.findall(r"Type\s*:\s*(.*?)\nSlack\s*:\s*(-?[\d.]+)",sta):
    m=re.match(r"(Slow|Fast) 1100mV (\d+C) Model (Setup|Hold)",ty)
    if m: best[m.groups()]=min(best.get(m.groups(),9e9),float(sl))
for k in sorted(best): print("   %s %s %s worst slack %+.3f"%(k[0],k[1],k[2],best[k]))
if best: print("   ALL POSITIVE" if all(v>=0 for v in best.values()) else "   NEGATIVE SLACK")
'''


def cmd_status(a):
    dirs = ssh(f"ls -d {BASE}/{a.name}-s* 2>/dev/null", check=False).split()
    if not dirs:
        sys.exit(f"no stage named {a.name} on the VM")
    for d in dirs:
        print(ssh(f"python3 - {d}", inp=STATUS_PY).rstrip())


def cmd_collect(a):
    d = stage_dir(a.name, a.seed)
    out = ROOT / "work/diagnostics" / a.name
    out.mkdir(parents=True, exist_ok=True)
    dst = out / f"ap_core_s{a.seed}.rbf"
    r = subprocess.run(SCP + [f"taualpha@127.0.0.1:{d}/src/fpga/output_files/ap_core.rbf", str(dst)])
    if r.returncode != 0:
        sys.exit("copy failed (has the fit finished?)")
    remote = ssh(f"sha256sum {d}/src/fpga/output_files/ap_core.rbf").split()[0]
    local = hashlib.sha256(dst.read_bytes()).hexdigest()
    if remote != local:
        sys.exit(f"hash mismatch after copy: VM {remote} vs local {local}")
    print(f"{dst}\nsha256 {local}  (verified against the VM copy)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("launch"); l.add_argument("name"); l.add_argument("--append"); l.add_argument("--seed", action="append", type=int)
    l.add_argument("--force", action="store_true"); l.set_defaults(fn=cmd_launch)
    s = sub.add_parser("status"); s.add_argument("name"); s.set_defaults(fn=cmd_status)
    c = sub.add_parser("collect"); c.add_argument("name"); c.add_argument("--seed", type=int, required=True); c.set_defaults(fn=cmd_collect)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
