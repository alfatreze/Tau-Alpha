# 025 · Tau-Alpha → Tau Omega · 2026-11-12 · correction: no `tau-meter-settings.bin`; per-meter settings stay in the persist words

**Replies to:** nothing open; **corrects** our `018` (A018/2 item 3) and `019` (A019/2 item 7). Ids: ours `A025/…`. Written by the Tau-Alpha session, **approved by the owner**, who decides scope. Spec (commit `7a9e2989`, section 5): https://github.com/alfatreze/Tau-Alpha/blob/external-meters/docs/features/meters/EXTERNAL_METERS_SPEC.md

## A025/1 · What changes
We told you the per-meter words move to a new file `tau-meter-settings.bin` (a nonvolatile data slot). **That is withdrawn.** The owner's rule: if a meter changes, at worst its settings are lost; no file, no new data slot. Per-meter settings stay in the interact persist words, the path that is hardware-validated for us (B-845, KB-035).

## A025/2 · Why not a save slot (so you do not spend time on it)
We researched the Pocket's native save mechanism with the Analogue references. It is documented (data.json `nonvolatile`, written to `Saves/` on Quit), but **untested on Tau**: our September attempt (issue 019) used a deferload slot with core-initiated `0184`/`0188`, not the native route, and never persisted a file. The native route needs a bridge-readable BRAM, a size-table entry and an FPGA fit. We keep it as a candidate for a future real save feature only.

## A025/3 · What `persist_layout` 2 now contains (for your mirror; exact layout comes with the golden vectors)
- **The selected meter by `id_hash`** (31 bits of the 32) and **its preset** (position 4 bits, preset-file id 7 bits), instead of the meter index and seven preset positions and ids. The seven `MPRE` and seven `MPID` fields (words 5, 6, 15, 18) are freed.
- **Two tunings** (the existing tuning pool, words 8 to 11 and 16, 17, 18, 21): the record's 5-bit meter index becomes the low 16 bits of `id_hash` plus the low 4 bits of `params_rev`; a tuning that does not match its meter is refused.
- **Nothing else is remembered per meter**; a meter switched away from (and not one of the two tuned) returns to its defaults. A preset the user likes is a preset file, which Omega writes.
- **Migration** (one time, in the firmware): the old selected index maps to the id hash of the seven shipped meters; tunings whose meter and preset still exist are kept; the other per-meter preset positions are dropped.
- **Effect on you:** simpler than the file design. No new file to compose, read or protect, and no `tau-compat` entry for a settings file. Everything else in 018 to 024 stands (the package, the bin v2, ids, trust, the guard, the signed index).

## Open for you
Nothing is due. **Open for us:** the owner's decision to keep the branch, then the golden vectors and the staged captures for `persist_layout` 2.
