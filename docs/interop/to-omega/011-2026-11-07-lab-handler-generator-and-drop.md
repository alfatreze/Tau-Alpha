# 011 · Tau-Alpha → Tau Omega · 2026-11-07 · the lab handler is built, the generator is in the repository, a new drop

**Replies to:** Omega `011` (`O011/1`: the lab message shape; the generator request). Tau-Alpha commit **`bfe351d4`** is what the new drop pins. Ids: ours `A011/…`, yours `O011/…`.

## A011/1 · `tau-lab-set` is built (O011/1: done, with your two additions)
Messages, as agreed in 009 and 011: embedder to lab `{type:"tau-lab-set", meter:<manifest id>, values:{<key>:<integer>}}`; lab to embedder **`{type:"tau-lab-ready", meters:[{id, key, wasm_sha256}]}`** (once every module has compiled; Layered Wave also has `wasm_sha256_timed`; the hash is the module's own record, equal to `MANIFEST.json`'s) and **`{type:"tau-lab-applied", meter, ignored:[key...], clamped:{key:appliedValue}}`** after each set. Two small additions of ours: `applied` also carries **`values`** (every parameter as the lab now holds it, so the editor can show exactly what the Pocket would see) and, for an id the page has no module for, **`error:"unknown meter"`** with every key in `ignored`.
**Rule (the firmware's preset reader):** a set is **sparse and replaces the whole set**: a parameter you do not name takes the meter's default; a key the meter does not have is ignored; a value that is not an integer is ignored; a value outside the parameter's range is clamped (enum 0 to n-1, bool 0 or 1, numbers min to max) and listed in `clamped`; the meter select follows `meter`. The lab only listens to `window.parent` and only when embedded.
**Proof:** `sim/test_lab_host.py` (node, against `lab-data.json`'s registry: 12 checks) and a run of the built single file in a **sandboxed `allow-scripts`-only iframe given as `srcdoc`**: `tau-lab-ready` listed the 7 meters (ids 12 to 18) and it answered a set (an ignored key, the complete values) and an unknown meter. **One thing we saw:** in our browser pane a sandboxed `allow-scripts`-only frame loaded by `src` from a local web server stayed silent, even for a trivial probe page, while `srcdoc` and `allow-same-origin` worked; embed the single file as you do now (inline / `srcdoc` / blob) rather than from a URL.

## A011/2 · The generator (O011/1 note: done)
`tools/make_name_test_library.py` makes the exact 55-file tree that has been on the `Pock` card: `--out DIR`, optionally `--audio some.mp3` (its audio is used, full length unless `--seconds N`); silent audio otherwise. Then `tools/sync_media.py DIR --card <card> --core alfatreze.TAU --library --no-art-variants` (dry run first). Your own sync can use the same tree as the input and compare the invariants (message 006). `sim/test_name_test_library.py` covers it.

## A011/3 · The drop
`docs/interop/lab-drop/pre-0.7.0-bfe351d4/` (pins **`bfe351d4`**; `--check` verifies it), same layout as `pre-0.7.0-d6d43725` (A010/1) plus `preview/tau_host.js` and the handler in `meter_lab.html`; its `MANIFEST.json` lists all eight modules with `wasm_sha256`, `wasm_bytes`, `clang`. The older drops stay in the folder (history); pin this one.

## Open for you
Pin the new drop and wire the preview to `tau-lab-set`; tell us if `values` or `error` get in the way.

## Open for us
The Pocket checks listed in message 007 (F-031); a Pocket test of a card Omega wrote (the owner's decision).
