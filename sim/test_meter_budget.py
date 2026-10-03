#!/usr/bin/env python3
"""tools/meter_budget.py on synthetic symbol dumps: a meter inside its ceilings passes, one byte over fails, a section is counted in the right class,
and a manifest whose symbol prefixes match nothing is reported (a rename must not make a budget pass silently)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import meter_budget as mb

def dump(rows):
    return "\n".join("%08x l     F %s\t%08x %s" % (0x1000 + i, sec, size, name) for i, (sec, size, name) in enumerate(rows))

def main():
    fails = 0
    def ok(name, cond):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + name)
        fails += 0 if cond else 1
    ms = {m["key"]: m for m in mb.manifests()}
    ok("manifests with a budget are found (layered_wave, chladni, vu_master, winamp_bars, winamp_scope)", {"layered_wave", "chladni", "vu_master", "winamp_bars", "winamp_scope"} <= set(ms))
    lw = ms["layered_wave"]; c = lw["budget"]
    base = [(".cold_text", c["cold"] - 100, "lw_tick"), (".cold_data", 100, "lw_tables"), (".bss", c["hot_ram"], "lw_state"), (".psram_state", c["psram_state"], "lw_hist"), (".text", c["hot_rom"], "lw_x")]
    full = dump(base + [(".cold_text", 5, "chl_a"), (".bss", 1, "chl_b"), (".cold_text", 5, "vum_a"), (".bss", 1, "wviz_disp"), (".cold_text", 5, "wviz_bars_tick"), (".cold_text", 5, "wviz_scope_tick")])
    rows, bad = mb.check(full)
    ok("a meter exactly at every ceiling passes", not [b for b in bad if b.startswith("layered_wave")])
    got = [r for r in rows if r["meter"] == "layered_wave"][0]["measured"]
    ok("cold counts .cold_text + .cold_data, hot_ram counts .bss, psram_state counts .psram_state", got["cold"] == c["cold"] and got["hot_ram"] == c["hot_ram"] and got["psram_state"] == c["psram_state"])
    over = dump(base + [(".bss", 1, "lw_extra")] + [(".cold_text", 5, "chl_a"), (".cold_text", 5, "vum_a"), (".cold_text", 5, "wviz_bars_tick"), (".cold_text", 5, "wviz_scope_tick")])
    _, bad = mb.check(over)
    ok("one byte over a ceiling is reported", any("layered_wave: hot_ram" in b for b in bad))
    _, bad = mb.check(dump([(".text", 4, "unrelated")]))
    ok("prefixes that match nothing are reported, not passed", sum("no symbols matched" in b for b in bad) == len(ms))
    print("PASSED (0 failures)" if not fails else "FAILED (%d)" % fails)
    sys.exit(1 if fails else 0)

main()
