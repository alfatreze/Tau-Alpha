import re, sys
S = sys.argv[1] + "/lw/"; var = sys.argv[2]; root = sys.argv[3]
txt = open(root + "/fw2/generated/lw_presets.h").read()
rows = re.findall(r'\{ ([\d, ]+) \},\s+/\* (.*?) \*/', txt)
def rd(p, fs, nf):
    t = open(S + "pkv_%s_%d_%d_%d.txt" % (var, p, fs, nf)).read()
    return int(re.search(r"TOTAL (\d+)", t).group(1)), int(re.search(r"MISSES (\d+)", t).group(1))
print("%-12s %-4s | %8s %7s %8s | %8s %7s %8s" % ("preset", "view", "win ins", "win mis", "win Mcyc", "fs ins", "fs mis", "fs Mcyc"))
tot = [0, 0]
for p, (v, nm) in enumerate(rows):
    v = [int(x) for x in v.split(",")]
    o = []
    for fs in (0, 1):
        a = rd(p, fs, 3); b = rd(p, fs, 13)
        di = (b[0] - a[0]) / 10.0; dm = (b[1] - a[1]) / 10.0
        o.append((di, dm, (di * 1.65 + dm * 253) / 1e6))
    print("%-12s %-4s | %8.0f %7.0f %8.2f | %8.0f %7.0f %8.2f" % (nm, "HIST" if v[0] == 0 else "SPEC", *o[0], *o[1]))
