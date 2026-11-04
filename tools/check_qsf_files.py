#!/usr/bin/env python3
"""Quartus-free consistency check of src/fpga/ap_core.qsf (and the .qip files it pulls in).

1. every source/constraint file the QSF names (VERILOG_FILE, SYSTEMVERILOG_FILE, QIP_FILE, SIP_FILE, SDC_FILE, ...) exists;
2. every module that is DEFINED in a file under src/fpga and INSTANTIATED somewhere in the listed sources or the .vh includes is defined in a file the QSF lists
   (an instantiated module whose file is not listed would synthesise as a black box or fail the fit);
3. a source file under src/fpga that defines modules but is not listed is reported as a warning when nothing instantiates its modules (dead file).
Exit 1 on problems 1 and 2. Usage: tools/check_qsf_files.py [path/to/ap_core.qsf]
"""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KINDS = ("VERILOG_FILE", "SYSTEMVERILOG_FILE", "QIP_FILE", "SIP_FILE", "SDC_FILE", "MIF_FILE", "HEX_FILE", "TCL_SCRIPT_FILE")
ASSIGN = re.compile(r"set_global_assignment\s+(?:-\w+\s+\"?\w+\"?\s+)*?-name\s+(\w+)\s+(.+)")
MACRO = re.compile(r"set_global_assignment\s+-name\s+VERILOG_MACRO\s+\"?(\w+)")
SRC_EXT = (".v", ".sv")


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


def active_text(t, macros):
    """Drop the branches of `ifdef/`ifndef/`elsif/`else that the default build (macros = the QSF VERILOG_MACROs plus every `define in the tree) does not take."""
    out, stack = [], []   # stack of (taking, any_taken, parent_active)
    for line in t.split("\n"):
        m = re.match(r"\s*`(ifdef|ifndef|elsif|else|endif)\b\s*(\w+)?", line)
        if m:
            d, name = m.groups()
            parent = all(x[0] for x in stack)
            if d in ("ifdef", "ifndef"):
                take = (name in macros) == (d == "ifdef")
                stack.append([take, take])
            elif d == "elsif" and stack:
                take = (not stack[-1][1]) and name in macros
                stack[-1] = [take, stack[-1][1] or take]
            elif d == "else" and stack:
                stack[-1] = [not stack[-1][1], True]
            elif d == "endif" and stack:
                stack.pop()
            continue
        if all(x[0] for x in stack):
            out.append(line)
    return "\n".join(out)


def _path_of(arg, base):
    arg = arg.strip()
    m = re.search(r'\[file join \$::quartus\(qip_path\) "?([^"\]]+)"?\]', arg)
    if m:
        return (base / m.group(1)).resolve()
    return (base / arg.strip('"')).resolve()


def listed_files(qsf):
    """(kind, path) for every file assignment in the qsf, following QIP_FILEs (paths relative to the qip's own folder)."""
    out, seen, todo = [], set(), [(qsf, qsf.parent)]
    while todo:
        f, base = todo.pop()
        if f in seen or not f.exists():
            continue
        seen.add(f)
        for line in f.read_text().splitlines():
            m = ASSIGN.match(line.strip())
            if not m or m.group(1) not in KINDS:
                continue
            p = _path_of(m.group(2), base)
            out.append((m.group(1), p))
            if m.group(1) == "QIP_FILE":
                todo.append((p, p.parent))
    return out


def check(qsf, src_root=None):
    qsf = Path(qsf).resolve()
    src_root = Path(src_root).resolve() if src_root else qsf.parent
    files = listed_files(qsf)
    problems, warnings = [], []

    def rel(p):
        return p.relative_to(src_root) if src_root in p.parents else p

    for kind, p in files:
        if not p.exists():
            problems.append(f"{kind} names a file that does not exist: {rel(p)}")
    listed = {p for _, p in files if p.exists()}
    raw = {}
    for p in sorted(src_root.rglob("*")):
        if p.suffix in SRC_EXT + (".vh",) and p.is_file():
            raw[p] = strip_comments(p.read_text(errors="replace"))
    macros = {m.group(1) for m in MACRO.finditer(qsf.read_text())}
    for t in raw.values():
        macros |= set(re.findall(r"^\s*`define\s+(\w+)", t, flags=re.M))
    texts = {p: active_text(t, macros) for p, t in raw.items()}
    defs = {}
    for p, t in texts.items():
        if p.suffix in SRC_EXT:
            for m in re.finditer(r"^\s*module\s+(\w+)", t, flags=re.M):
                defs.setdefault(m.group(1), p)
    users = [p for p in texts if p in listed or p.suffix == ".vh"]
    inst_in = {}
    for mod, df in defs.items():
        pat = re.compile(r"(?:^|[;\s)])" + re.escape(mod) + r"\s*(?:#\s*\(|[\w\\\.\$]+\s*(?:\[[^\]]*\]\s*)?\()", re.M)
        for u in users:
            if u != df and pat.search(texts[u]):
                inst_in.setdefault(mod, []).append(u)
    for mod, df in sorted(defs.items()):
        if mod in inst_in and df not in listed:
            problems.append(f"module {mod} (defined in {rel(df)}) is instantiated in {rel(inst_in[mod][0])} but that file is not in the QSF")
    for mod, df in sorted(defs.items()):
        if df not in listed and mod not in inst_in:
            warnings.append(f"{rel(df)}: not in the QSF and nothing instantiates {mod} (dead file, fine for simulation-only sources)")
    return problems, sorted(set(warnings))


def main(argv):
    qsf = Path(argv[1]) if len(argv) > 1 else ROOT / "src/fpga/ap_core.qsf"
    problems, warnings = check(qsf)
    for w in warnings:
        print("warn  " + w)
    for p in problems:
        print("FAIL  " + p)
    if not problems:
        print(f"ok    {qsf.name}: all listed files exist, every instantiated module's file is listed")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
