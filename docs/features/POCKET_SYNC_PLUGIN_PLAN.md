# Pocket plugin for TAU (Pocket Sync and pupdate): plan

Status (2026-11-11, B-1028): **the plugin now lives in its own repository, https://github.com/alfatreze/tau-pocket-sync (MIT, public, nothing shared with this tree by path); this file is the original plan and the decisions record.** Built so far there: the scaffold and **Check the card** (read only). The cover sidecars are decided for 0.1 (owner, 2026-11-11: "do covers"). Original status: plan only, nothing built. Owner's ask: the simplest possible sync, either a whole music folder or "rebuild the library from the files on the card"; a separate build target published as its own GitHub repository (like `alfatreze/spectrumdeck-pocket-sync`); small useful extras; no re-encoding; a nudge toward Tau Omega.

## 1. What a Pocket plugin is (read from the reference repo and the host's behaviour)
- **Reference:** `openfpga-library/pocket-plugin` (MIT). One plugin runs unchanged in **Pocket Sync and pupdate**. It is a WebAssembly (`wasm32-wasip1`, Extism) module with `start()` and `handle_response()`; it answers with JSON `Choice`, `Text` or `Exit` messages and may call two host functions, `print_msg` (live log, no newline added) and `open_url`.
- **Release:** a GitHub repository whose release has exactly `plugin.wasm` and `plugin.json` (name, description, `allowed_hosts`, logo); the host loads the latest release and keeps it updated. SpectrumDeck already ships this way (`alfatreze/spectrumdeck-pocket-sync`, MIT, Rust, Extism tests, a demo host).
- **What the plugin can touch:** the card as `pocket/` (whole card, read/write) and a private desktop folder as `host/` (Pocket Sync: `<app data>/<owner>-<repo>/host`). **No folder picker and no other desktop path.** Single-threaded; no graceful cancel (the host sends `Kill`, the plugin only stops when it returns `Exit`), so every write must be temp file, verify, rename.
- **Licence care (rule 7):** Pocket Sync itself is AGPL-3.0. We target only the MIT reference ABI; we do not copy Pocket Sync code. (Omega's `ARCHITECTURE.md` still says Pocket Sync has no plugin system: out of date, it has had one since 2026.)

## 2. What the plugin does (version 0.1)
**Primary, and the simplest honest sync: "Rebuild library from the card".** The music is already on the card (copied with Finder or any tool). The plugin scans `pocket/Assets/tau/common`, reads the tags, builds `tau-library.tdb` and the name sidecar (`tau-names.json` and `.bin`) exactly as Tau Omega and `tools/tau_library.py` do, and replaces them safely. Always shows a **plan first** (tracks, albums, playlists, what will be skipped, name problems) and asks before writing.

**Secondary: "Add music from the plugin's folder".** Because of the sandbox, a whole desktop folder can only be synced if the user puts it in the plugin's `host/` folder (we show the exact path). The plugin then copies additively (skips what is already on the card with the same size and hash, never overwrites different files without asking), applies the card-safe name rules, and rebuilds the library. Honest limit: it means copying twice on the desktop; for most users "copy with Finder, then Rebuild" is faster. Decision for the owner: ship it in 0.1 or leave it for 0.2.

**Sidecars the rebuild writes:** the **text** sidecar (`tau-names.json` and `tau-names.bin`, originals of names and tags, bound to the index build id) and, per item 6 of section 3, the **image** sidecars (`<album>/tau-art/cover_128.pal256.timg`). Existing ones are kept; only missing or stale ones are written.

**Menu:** Rebuild library, Check library (read only), Add music from the plugin folder, Get Tau Omega (opens the page), Exit.

## 3. Small extras worth having (each is read-only or asks first)
1. **Plan and report before any write** (counts, time estimate, skipped files).
2. **Skip unsupported files and list them.** TAU plays MP3 and FLAC (and `.m3u`/`.m3u8` playlists); anything else (m4a, ogg, opus, wav, aac, ape) is left alone and listed as "needs conversion: use Tau Omega". Albums with no playable track are not indexed.
3. **Library check:** index against files (missing files, files not in the index, a stale name sidecar, tracks over the 200-byte path budget, names the Pocket cannot open).
4. **Card-safe names, only when needed:** rename only names the Pocket cannot open (the rules of `CARD_NAME_RULES.md`), keep the original in the sidecar, ask first.
5. **Junk sweep:** `._*` and `.DS_Store` stubs, empty folders, duplicate files (same size and tag), listed, removed only on a yes.
6. **Cover sidecars (proposed for 0.1, was "later"):** for every album folder without `tau-art/cover_128.pal256.timg`, make it from the album's folder image or the first track's embedded cover (the TIM1 palette-256, 128 px long side, no crop; the file Player 2 reads by that fixed name, so covers stop decoding a JPEG for seconds on every track change). Omega's `tau-core` already has the extractor and the encoder in pure Rust (its quantiser is its own, not byte-identical to `tools/tau_image.py`, which does not matter to the player); the plugin lists albums with no cover at all.
7. **Card sanity:** refuse politely if `Assets/tau` is missing; show which TAU version is installed (from the card's `core.json`).

Left out on purpose: re-encoding or conversion (needs LAME or ffmpeg, which do not run in the plugin); tag editing; core install; settings. Those are Tau Omega.

## 4. The Tau Omega nudge
At the end of every run and as a menu item (`open_url` to the Tau Omega releases page). Draft text: "Tau Omega does the rest: converts any format to MP3 or FLAC for TAU, edits tags and covers, keeps ReplayGain right, builds playlists, installs and updates TAU and your meter presets, checks the whole card. Free, for Mac, Windows and Linux." One line only, never blocking.

## 5. How it is built
- **Reuse, not rewrite:** Omega's `tau-core` (MIT, same owner) already has `scan` (tags), `build_index_with_names` (byte-exact with `tau_library.py`), `name_sidecar`, `card_name`, `index_read`. **Compile check on 2026-11-11 (`cargo check -p tau-core --target wasm32-wasip1`): 8 errors only**, all the disk-space calls of the `fs4` crate (not available on WASI: `available_space`, `total_space`, `allocation_granularity`) and one `serde` cfg in `name_sidecar.rs`; nothing else (the audio, image and text code builds). So the work is a small cfg shim in `tau-core` (an Omega change, owner's call, sent by interop message) or a trimmed copy in the plugin. Recommendation: the shim, so the plugin and Omega share one index builder and cannot drift. The plugin depends on `tau-core` by git tag.
- **Repository:** new, MIT, `alfatreze/tau-pocket-sync` (name to be confirmed): a Rust workspace with the plugin crate (`cdylib` for `wasm32-wasip1`, `extism-pdk`), `plugin.json`, a GitHub Action that builds on a tag and attaches `plugin.wasm` and `plugin.json`, a host-side test crate that runs the compiled wasm under Extism with a disposable card and host folder (SpectrumDeck's pattern), and a README with screenshots and the Tau Omega nudge. Tau Alpha gets only this plan and a link; no plugin code in this repository.
- **Tests:** the golden index built from the same music tree by `tau_library.py`, Omega and the plugin must be byte-identical (the name test library, `tools/make_name_test_library.py`, is the corpus); an interrupted write leaves the old index intact; unsupported files are listed and untouched; a read-only card fails cleanly.

## 6. Risks and unknowns (to measure, not assume)
- **Time and memory on a large library** (tens of thousands of tracks through WASI): unmeasured. Mitigation: print progress often; scan tags in one pass; measure on a 5,000-track tree before promising anything.
- **Host timeouts and cancel:** unknown for long calls; the plugin must be safe to stop at any point (temp, verify, rename).
- **pupdate:** the reference says the same plugin runs there; the mount names and the `host` folder there are unverified.
- **Installed Pocket Sync 7.2.1:** plugin loading and the exact `host` path must be tried in the real app (SpectrumDeck's notes did the same inspection at 7.0.1).
- **Dependencies' notices** (`deunicode` BSD-3 and others) belong in the repository's NOTICE file.

## 7. Steps
S0 spike (the `tau-core` shim, a plugin that rebuilds a library in the Extism test host, golden comparison) -> S1 plan, rebuild and check with the menu -> S2 safe copy from the plugin folder (if kept) and the extras -> S3 CI release and a trial in the real Pocket Sync -> S4 README, NOTICE, the Omega nudge text, a first release `v0.1.0`.

## 8. Decisions for the owner
1. Repository name and plugin name ("TAU Library"?).
2. Approve the small `tau-core` WASI shim in Omega (an interop message to Omega), or a trimmed copy.
3. Ship "Add music from the plugin's folder" in 0.1, or only the card-side rebuild.
4. Which extras from section 3 go into 0.1 (suggested: 1, 2, 3, 7; later 4, 5, 6).
