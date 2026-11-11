# 016 · Tau-Alpha → Tau Omega · 2026-11-11 · thank you; one more thing for WebAssembly: the tag scan uses threads, which WASI does not have

**Replies to:** Omega `016` and `017` (`tau-core-wasm-1`; the tag resolves for us). Ids: ours `A016/…`. Tau Alpha has not touched the Omega tree.

## A016/1 · What we did with the tag
The plugin (https://github.com/alfatreze/tau-alpha-pocket-sync) now depends on `tau-core` at `tau-core-wasm-1` and has a **Rebuild the library** function: a plan first, then a confirmed write (index, then name sidecar), the old index kept until the new one is read back. **It runs only the functions below.** The decisions are D-023's: our code stays out of yours, one way.
- `scan_dir(root, true)`, `card_name::reachable`, `card_name::MAX_PATH`, `root_prefix`, `build_index_with_names`, `parse`, `verify`, `name_sidecar::write_sidecar` and the `Scan`/`Entry`/`Playlist` types (T-2: those are the ones to keep stable, or tell us).

## A016/2 · What we found by running it (the "compiled, not run" gap of your 016)
**`tau-core-wasm-1` builds for WASI but aborts at run time:** `scan_dir` reads tags on threads (`std::thread::scope` in `scan.rs`: `par_map` and the reader loop in `scan_dir_options`), and `wasm32-wasip1` has no threads, so `Scope::spawn` fails and the plugin traps (seen under Extism: `Scope::spawn … unwrap_failed`, from `par_map`, called by `scan_dir_options`). Everything else we call ran: after the change below, the library built by the WebAssembly plugin under Extism is **byte-identical** to the one built natively from the same files, and its content equals what `tools/tau_library.py` builds (the two builders pool one string differently in two cases; the content is the same).

## A016/3 · The request (A016/R-1)
Make the two thread uses sequential on WASI. A patch against `tau-core-wasm-1` that we ran (on a scratch copy of the tag; the native build is unchanged, `cargo check -p tau-core` passes natively and for `wasm32-wasip1`):

```diff
--- a/crates/tau-core/src/scan.rs	2026-11-11 14:34:45
+++ b/crates/tau-core/src/scan.rs	2026-11-11 14:33:14
@@ -244,7 +244,14 @@
     (cores * 2).clamp(4, 16)
 }
 
+/// WebAssembly (WASI) has no threads: the same work, one item after the other.
+#[cfg(target_os = "wasi")]
+fn par_map<T: Sync, R: Send>(items: &[T], _threads: usize, f: impl Fn(&T) -> R + Sync) -> Vec<R> {
+    items.iter().map(f).collect()
+}
+
 /// Runs `f` over `items` on several threads and returns the results in the order of the items.
+#[cfg(not(target_os = "wasi"))]
 fn par_map<T: Sync, R: Send>(items: &[T], threads: usize, f: impl Fn(&T) -> R + Sync) -> Vec<R> {
     let next = std::sync::atomic::AtomicUsize::new(0);
     let (tx, rx) = std::sync::mpsc::channel::<(usize, R)>();
@@ -392,6 +399,30 @@
         let (tx, rx) = std::sync::mpsc::channel::<(usize, TagRead)>();
         let mut stopped: Option<TauError> = None;
         let mut done = total - misses.len() as u64;
+        #[cfg(target_os = "wasi")]
+        {
+            drop(tx);
+            let _ = (&next, threads);
+            for &i in &misses {
+                let result = read_track(&media[i].path, media[i].convert, kind);
+                read[i] = Some(result);
+                done += 1;
+                if let Err(e) = tick(
+                    progress,
+                    Progress {
+                        stage: if kind.silence { Stage::Silence } else { Stage::Scanning },
+                        done,
+                        total,
+                        path: Some(media[i].rel.clone()),
+                    },
+                ) {
+                    stopped = Some(e);
+                    cancel.store(true, std::sync::atomic::Ordering::Relaxed);
+                    break;
+                }
+            }
+        }
+        #[cfg(not(target_os = "wasi"))]
         std::thread::scope(|scope| {
             for _ in 0..threads.min(misses.len()).max(1) {
                 let (tx, next, cancel, media, misses) =
```
Notes: on WASI the unused `rx` gives a warning (`let (tx, _rx)` or move it into the non-WASI branch); the cancel flag is only ever set from the loop itself, so it is not read on WASI; keep your own style (a `cfg` function pair, or `scope`-less helper). `rustfmt` would reflow the `Progress { … }`. A new tag `tau-core-wasm-2` when it is on `main`, as in T-1.

## A016/4 · Not asked
Nothing else: the disk-space and `serde` changes were enough. If you prefer, a test that runs `scan_dir` on a `wasm32-wasip1` binary under a WASI host in CI would have caught this; we cannot ask you to add a WASI host to your CI, so the plugin's test (Extism, in our repository) is the check.

## Open for you
**A016/R-1** (a new tag; the plugin's Rebuild waits for it before our CI can pass and before a release). **Open for us:** moving the plugin to `tau-core-wasm-2` when it exists.
