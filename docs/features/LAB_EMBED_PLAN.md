# Lab embed contract: plan for Omega's message 014 (2026-11-07)

Status: plan only, nothing built. Host-only work (lab page, `lab-data.json`, one new drop); no firmware, no Pocket. Omega's asks are `O014/R-1` to `R-5`, findings `F-1` to `F-6`, questions `Q-1`, `Q-2` (Omega repo `docs/interop/to-alpha/014-...md`).

## Q-1 answered (from the code)
- Table rows are `METER_SLOTS` = 7 (`fw2/generated/meter_registry.h`, generated from the manifests). Row 0 is **Winamp Bars** (built in, `METER_N` = 1); rows 1 to 6 are the pack-only meters: 1 Winamp Scope, 2 Chladni, 3 Layered Wave, 4 Master VU, 5 Waterfall, 6 Sound Map.
- **Winamp Bars is slot 0, yes.** But "seven extra" (message 005) is the PSRAM pack capacity (`MTR_PACK_SLOTS` = 8), not what the player can list: persisted preset fields are `SF_METERS_MAX` = 7 and the Settings list shows `MLIST_VIS` = 7 rows. Both have a static assert. **Today a bundle can add at most six meters besides Winamp Bars (7 slots in all, all 7 used by the shipped set).** Omega should draw "N of 7", not "N of 8"; more than 7 needs a firmware change (persist fields, list scrolling), which is not planned.

## Plan (one lab drop, in this order)
1. **R-4 per-meter view data** (small): `lab-data.json` gets `views` per meter: `small {x,y,w,h}` and `full {x,y,w,h}` or `null` when the meter has no full-screen view. Source of truth: a `views` block in each meter's `meter.json` (checked by the schema test), not the lab page. Needs one fact per meter from the firmware (does a full-screen figure exist); the lab's generic `fullfig` stays for the preview. Answers F-6.
2. **R-1 embed mode and F-4**: `#embed` fragment (and a `tau-lab-embed` message): no heading, panel, parameter list or sticky stage, transparent background, canvas drawn at a fixed 400 css px with no fractional scale (also in the normal page); the page posts `tau-lab-size {w,h}`. Removes Omega's stylesheet (F-1) and the sizing part of F-2.
3. **R-2 options**: the preview controls become a table in the page (`id, label, type, choices|range, value, applies_to`), sent as `tau-lab-options` after `tau-lab-ready`; `tau-lab-option {id, value}` sets one. Removes the element-id coupling (F-2 a).
4. **R-3 and R-5 cost as data**: `tau-lab-info` (cost summary fields and the per-option table as rows and cells) and `tau-lab-cost` (every other frame: this frame's commands plus two windows). **Verdict window:** keep the page's own (worst of the last 120 frames, about 3 s; it is the hardware-meaningful one) and also send `recent` (last 30 frames, mean and worst) so the budget bar need not hook `TauCost.summarize`. **F-5:** clear the cost window when a parameter or option changes (proposed; the figures then follow a change at once).
5. **F-3 contract**: `lab-data.json` gets `contract: 1` and `docs/features/LAB_EMBED_CONTRACT.md` lists the messages and the element ids that stay stable; a drop that breaks them must bump `contract`. `sim/test_lab_host.py` (node) grows a check per message; the drop carries the contract version in `MANIFEST.json`.
6. **Drop and message**: `tools/make_lab_drop.py` → new `docs/interop/lab-drop/pre-0.7.0-<commit>/`, then message 015 to Omega (ids `A015/...`) answering Q-1 and R-1..R-5. Omega's wiring is theirs.

## Owner decisions needed
- Go ahead with the drop (steps 1 to 6), or only R-4 now?
- F-5: clear the cost window on every setting change (changes the lab's own numbers slightly).
- Which meters have a full-screen figure (step 1) if the manifests do not say.

## Not in this plan
`Q-2` (P1 manifest key for a user-chosen `tau-packs.bin` and a pack library asset) is a timing decision for the owner; `to-omega/005` D-O01 stays the contract until then.
