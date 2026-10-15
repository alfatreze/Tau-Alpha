#!/usr/bin/env python3
"""Player 2, stage S0 (docs/features/PLAYER_V2_SPEC.md section 16): the global ownership map (the R0 gate of the rewrite report).

Reads fw/player.c and every fw/*.inc it includes (they are one translation unit), finds the file-scope variables and the function definitions, and reports for
each variable which clusters (function-name prefix, or the .inc file for functions defined there) READ it and which WRITE it. A variable written by more than
one cluster, or touched by more than three, is a design flag: it needs one owner before code moves (spec principle P2 / P7).

A lexical analysis, not a compiler: comments, strings and preprocessor lines are removed; a function is a depth-0 `name(...) { ... }`; a variable is a depth-0
declaration without a parameter list. Macros that hide a variable name are not followed (a limit, written in the report). Output: Markdown to stdout or --out.
"""
import argparse, re, sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "fw"

def strip(src):
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2); i = n if j < 0 else j + 2; out.append(" ")
        elif c in "\"'":
            q = c; j = i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            out.append('""' if q == '"' else "0"); i = j + 1
        else:
            out.append(c); i += 1
    s = "".join(out)
    # preprocessor lines (with continuations) removed, but #if blocks' bodies are kept (all configurations are analysed together)
    s = re.sub(r"(?m)^[ \t]*#(?:[^\n\\]|\\\n)*\n", "\n", s)
    return s

def load_unit():
    main = (FW / "player.c").read_text()
    files = {"player.c": main}
    for m in re.finditer(r'#include\s+"([^"]+\.inc)"', main):
        p = FW / m.group(1)
        if p.exists(): files[m.group(1)] = p.read_text()
    # an .inc may include another .inc
    changed = True
    while changed:
        changed = False
        for name, text in list(files.items()):
            for m in re.finditer(r'#include\s+"([^"]+\.inc)"', text):
                if m.group(1) not in files and (FW / m.group(1)).exists():
                    files[m.group(1)] = (FW / m.group(1)).read_text(); changed = True
    return files

def toplevel(text):
    """Yield (kind, header, body) for each depth-0 construct: ('var', stmt, None) or ('func', header, body)."""
    s = strip(text); i, n = 0, len(s); start = 0; depth_p = 0
    stmt_start = 0
    while i < n:
        c = s[i]
        if c == "(": depth_p += 1
        elif c == ")": depth_p -= 1
        elif c == ";" and depth_p == 0:
            yield ("stmt", s[stmt_start:i], None); stmt_start = i + 1
        elif c == "{" and depth_p == 0:
            hdr = s[stmt_start:i]; d = 1; j = i + 1
            while j < n and d:
                d += (s[j] == "{") - (s[j] == "}"); j += 1
            body = s[i:j]
            # a function definition has a parameter list directly before the brace; an initializer (`= {`), struct or enum does not
            if re.search(r"\)\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?$", hdr.strip(), re.S) and "=" not in hdr.split("(")[0]:
                yield ("func", hdr, body); stmt_start = j
            else:
                # initializer / aggregate: it continues to the next ';' as part of the same statement
                k = j
                dp = 0
                while k < n and not (s[k] == ";" and dp == 0):
                    dp += (s[k] == "(") - (s[k] == ")"); k += 1
                yield ("stmt", s[stmt_start:i] + s[j:k], None); stmt_start = k + 1; j = k + 1
            i = j; continue
        i += 1

KW = set("static const volatile unsigned signed int char short long float double void struct union enum extern register inline uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t size_t uintptr_t".split())

def declared_names(stmt):
    st = re.sub(r"__attribute__\s*\(\(.*?\)\)", " ", stmt, flags=re.S)
    st = re.sub(r"\b(?:COLD_DATA|COLD_TEXT|COLD_FN\d?|COLD_SR|MTR_PSRAM|HOT)\b", " ", st)
    if re.match(r"\s*(typedef|enum\b[^=]*$|struct\b[^=*]*$|union\b[^=*]*$)", st): return []
    if "(" in st and not re.search(r"\(\s*\*\s*\w+\s*\)", st) and "=" not in st.split("(")[0]: return []   # a prototype
    names = []; depth = 0; cur = []
    for ch in st:
        if ch in "([{": depth += 1
        elif ch in ")]}": depth -= 1
        if ch == "," and depth == 0: names.append("".join(cur)); cur = []
        else: cur.append(ch)
    names.append("".join(cur))
    out = []
    for k, part in enumerate(names):
        head = part.split("=")[0]
        m = re.findall(r"[A-Za-z_]\w*", re.sub(r"\[[^\]]*\]", " ", head))
        m = [x for x in m if x not in KW]
        if m: out.append(m[-1] if k else m[-1])
    return out

PREFIX = [("ui_", "ui"), ("fb_", "gfx"), ("helios", "gfx"), ("pl_", "queue"), ("lib_", "queue"), ("list_", "queue"), ("flac_", "decode"), ("fl_", "decode"),
          ("mp3_", "decode"), ("mt_", "decode"), ("refill", "stream"), ("target_", "stream"), ("slot_", "stream"), ("pcm_", "aout"), ("cymo_", "aout"),
          ("rg_", "aout"), ("gp_", "aout"), ("vol_", "aout"), ("gain_", "aout"), ("tempo", "decode"), ("wsola", "decode"), ("art_", "art"), ("id3_", "tags"),
          ("read_track", "tags"), ("vbr_", "tags"), ("set_", "settings"), ("settings", "settings"), ("meter", "meters"), ("viz_", "meters"), ("wviz", "meters"),
          ("vum_", "meters"), ("chladni", "meters"), ("lw_", "meters"), ("spec_", "meters"), ("wave", "meters"), ("sw_", "diag"), ("chk_", "diag"),
          ("bt_", "diag"), ("mw_", "diag"), ("info_", "diag"), ("stress", "diag"), ("dbg_", "diag"), ("load_track", "transport"), ("main", "app"),
          ("poll_input", "app"), ("cold_", "platform"), ("th_", "ui"), ("dbuf", "gfx"), ("hal_", "settings")]
INC_CLUSTER = {"library.inc": "queue", "playlist.inc": "queue", "art.inc": "art", "settings.inc": "settings", "settingsui.inc": "settings", "suite.inc": "diag",
               "stress.inc": "diag", "chladni.inc": "meters", "vu_master.inc": "meters", "layered_wave.inc": "meters", "fullscreen.inc": "ui", "helios.inc": "gfx",
               "blit_probe.inc": "gfx", "halcyon.inc": "settings", "halcyon_page.inc": "settings", "assets.inc": "settings", "timg.inc": "art", "cold.inc": "platform",
               "report.inc": "diag", "info_export.inc": "diag", "pcm_ring_fw.inc": "aout", "mp3_poly_hw.inc": "decode", "flac_lpc_hw.inc": "decode",
               "stress_defs.inc": "diag", "settings_diag.inc": "diag", "wvcfg_export.inc": "diag", "fw_extra.inc": "app"}

def cluster_of(name, fname):
    for p, c in PREFIX:
        if name.startswith(p): return c
    if fname in INC_CLUSTER: return INC_CLUSTER[fname]
    return "app"

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out"); a = ap.parse_args()
    files = load_unit()
    variables = {}            # name -> file
    funcs = []                # (name, file, body)
    for fname, text in files.items():
        for kind, hdr, body in toplevel(text):
            if kind == "func":
                m = re.search(r"(\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?$", hdr.strip(), re.S)
                if m: funcs.append((m.group(1), fname, body))
            else:
                for nme in declared_names(hdr):
                    if re.match(r"^[A-Za-z_]\w*$", nme) and nme not in variables: variables[nme] = fname
    names = sorted(variables, key=len, reverse=True)
    pat = re.compile(r"\b(" + "|".join(re.escape(x) for x in variables) + r")\b") if variables else None
    wr = re.compile(r"^\s*(?:\[[^\]]*\]\s*)*(?:\.\w+\s*|->\w+\s*|\[[^\]]*\]\s*)*(=(?!=)|\+\+|--|[-+*/|&^%]=|<<=|>>=)")
    readers, writers = defaultdict(set), defaultdict(set)
    nfunc = defaultdict(int)
    for fn, fname, body in funcs:
        cl = cluster_of(fn, fname); nfunc[cl] += 1
        for m in pat.finditer(body):
            v = m.group(1)
            readers[v].add(cl)
            after = body[m.end():m.end() + 40]; before = body[max(0, m.start() - 3):m.start()]
            if wr.match(after) or re.search(r"(\+\+|--)\s*$", before) or re.search(r"&\s*$", before):
                writers[v].add(cl)
    rows = []
    for v, f in variables.items():
        r, w = readers.get(v, set()), writers.get(v, set())
        rows.append((v, f, sorted(r), sorted(w)))
    multi_w = [x for x in rows if len(x[3]) > 1]
    wide = [x for x in rows if len(set(x[2]) | set(x[3])) > 3]
    unused = [x for x in rows if not x[2]]
    out = []
    P = out.append
    P("# Player 2 S0: global ownership map of the present firmware\n")
    P("Generated by `tools/player2/globals_map.py` (a lexical analysis of `fw/player.c` and the %d `.inc` files it includes; macros that hide names are not followed, so a count is a lower bound).\n" % (len(files) - 1))
    P(f"* file-scope variables found: **{len(variables)}**; function definitions: **{len(funcs)}**; clusters: {', '.join(f'{k} ({v})' for k, v in sorted(nfunc.items(), key=lambda kv: -kv[1]))}")
    P(f"* written by **more than one** cluster: **{len(multi_w)}** (each needs one owner before it moves)")
    P(f"* touched by **more than three** clusters: **{len(wide)}**")
    P(f"* never referenced inside a function body (macro-only use, MMIO views or dead): {len(unused)}\n")
    P("## Variables written by more than one cluster\n\n| variable | declared in | writers | readers |\n|---|---|---|---|")
    for v, f, r, w in sorted(multi_w, key=lambda x: (-len(x[3]), x[0])):
        P(f"| `{v}` | {f} | {', '.join(w)} | {', '.join(r)} |")
    P("\n## Variables touched by more than three clusters (read or write)\n\n| variable | declared in | clusters |\n|---|---|---|")
    for v, f, r, w in sorted(wide, key=lambda x: (-len(set(x[2]) | set(x[3])), x[0])):
        P(f"| `{v}` | {f} | {', '.join(sorted(set(r) | set(w)))} |")
    P("\n## Proposed owner of every variable (the one writer, or the declaring cluster when nobody writes it)\n")
    by_owner = defaultdict(list)
    for v, f, r, w in rows:
        owner = w[0] if len(w) == 1 else (cluster_of(v, f) if not w else "CONTESTED")
        by_owner[owner].append(v)
    for o in sorted(by_owner):
        vs = sorted(by_owner[o]); P(f"**{o}** ({len(vs)}): " + ", ".join(f"`{x}`" for x in vs[:80]) + (" ..." if len(vs) > 80 else "") + "\n")
    text = "\n".join(out) + "\n"
    if a.out: Path(a.out).write_text(text)
    else: sys.stdout.write(text)
    print(f"variables {len(variables)} functions {len(funcs)} multi-writer {len(multi_w)} wide {len(wide)}", file=sys.stderr)

if __name__ == "__main__": main()
