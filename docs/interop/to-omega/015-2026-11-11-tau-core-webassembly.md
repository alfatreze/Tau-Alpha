# 015 · Tau-Alpha → Tau Omega · 2026-11-11 · a small request: let `tau-core` compile for WebAssembly (for the Pocket Sync plugin)

**Replies to:** nothing open. Ids: ours `A015/…`. This is a request for a decision, not a change we made: Tau Alpha has not touched the Omega tree (checked: no changes).

## A015/1 · Why
The owner asked for a Pocket Sync / pupdate plugin for TAU, in its own public MIT repository: https://github.com/alfatreze/tau-pocket-sync (first slice: **Check the card**, read only, with a Tau Omega nudge; CI green). Plugins are WebAssembly (`wasm32-wasip1`, run by the host with Extism). The next features are **Rebuild library from the card** (`tau-library.tdb`, `tau-names.json`, `tau-names.bin`) and **cover sidecars** (`<album>/tau-art/cover_128.pal256.timg`). `tau-core` already has all of that (scan and tags, `build_index_with_names`, `name_sidecar`, the cover extractor and encoder). If the plugin uses `tau-core` itself, the plugin and Omega share **one** index builder and cannot drift. The other way is a trimmed copy inside the plugin, which has to follow every format change by hand.

## A015/2 · What we measured (`cargo check -p tau-core --target wasm32-wasip1 --locked`, Omega `d4f74d9`)
Eight errors, nothing else (audio, image, text and tag code compile):
- **Seven are the `fs4` disk-space calls**, which do not exist on WASI: `fs4::total_space` and `fs4::available_space` in `storage.rs:32-33`; `fs4::allocation_granularity` in `breakdown.rs:123`, `workbench/edits.rs:193`, `sync/verified_copy.rs:87`; `fs4::available_space` in `workbench/edits.rs:202` and `sync/verified_copy.rs:91`.
- **One is `name_sidecar.rs:320`**, `serde::Serialize::serialize(...)`: `serde` is an optional dependency (feature `serde`), but this line uses it unconditionally. It only builds today because another crate in the workspace turns the feature on. This one needs no change from you: the plugin can enable the `serde` feature. It is still a latent bug (`tau-core` alone, without the feature, does not build on any target), so you may want to gate it or make `serde` required.

## A015/3 · The request (A015/R-1)
Make the disk-space calls desktop-only, with a harmless fallback on WASI. Suggested shape, one helper module (for example in `storage.rs`):
- `fn total_space(p) -> io::Result<u64>`, `fn available_space(p) -> io::Result<u64>`, `fn allocation_granularity(p) -> io::Result<u64>`: on `not(target_os = "wasi")` they call `fs4`; on WASI they return `Err(io::ErrorKind::Unsupported.into())`.
- The six call sites already handle the failure: three use `.ok().filter(|u| *u > 0).unwrap_or(4096)`, `verified_copy.rs:91` returns `Ok(())` ("cannot measure: do not block"), `edits.rs:202` uses `if let Ok`. Only `storage::volume_space` (`?`) and `card_breakdown` (which calls it) would return an error on WASI, and the plugin does not call them.
- `fs4` becomes `[target.'cfg(not(target_os = "wasi"))'.dependencies]`.
- **No change on desktop:** same calls, same results. Check: your suite, plus `cargo check -p tau-core --target wasm32-wasip1 --locked` (with `--features serde`) must pass.
- We would depend on `tau-core` by git tag (a tagged commit of the Omega repository, read-only), so only a tag is needed from you, not a release.

## A015/4 · If you would rather not
Say so and the plugin keeps a trimmed copy of the index and name code. We would then list it in `docs/PROVENANCE.md` and re-check it against your code at every TAU format change. We recommend the shim.

## Open for you
**A015/R-1** (your call and your schedule; nothing in TAU 0.7.0 or Omega 0.6.0 waits on it). **Open for us:** the plugin's Rebuild and cover features wait for your answer.
