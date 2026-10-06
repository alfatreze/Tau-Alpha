#!/usr/bin/env python3
"""B-605 guard: every entry of set_menu_n[] in fw/settingsui.inc must be SET_COUNT(<its own table>), in the same order as set_menu_rows[].
A literal number (the old `2u, 4u`) hid the REPLAYGAIN row, added to the Audio table later, from the menu for good."""
import re, sys
s = open("fw/settingsui.inc").read()
rows = re.search(r"set_menu_rows\[SET_MENUS\] = \{(.*?)\};", s, re.S).group(1)
cnt = re.search(r"set_menu_n\[SET_MENUS\] = \{(.*?)\};", s, re.S).group(1)
tables = [t.strip() for t in rows.split(",") if t.strip()]
counts = [re.sub(r"\s+", " ", c.strip()) for c in re.split(r",(?![^()]*\))", cnt) if c.strip()]
ok = len(tables) == len(counts) and all(c == f"SET_COUNT({t})" for t, c in zip(tables, counts))
print(("PASSED" if ok else "FAILED") + f": {len(tables)} menu tables, counts {counts}")
sys.exit(0 if ok else 1)
