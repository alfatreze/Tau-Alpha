# v0.7.0 REHEARSAL: not a release

Built 2026-10-26 by `tools/make_release.py --player2 --release v0.7.0` in a throwaway worktree (`rehearsal-v0.7.0`, version bump and changelog heading only there). For Tau Omega to rehearse install, check-card and removal on CARDWRITE. **Do not publish, do not put on a Pocket as a release.** The real v0.7.0 will differ: features still to be chosen, its own bitstream and its own date.

* **Two cores since 2026-10-26 (D-U02):** `alfatreze.TAU` (the normal build: About, the support Report, no Check) and `alfatreze.TAU Diagnostics` (everything: the Check, the full counter table), each its own zip; the Diagnostics core is optional to install; both carry `tau-packs.bin` (optional, slot 9).
* Bitstream: `stats-b821` (the one DEV 185 runs), firmware built with `PACKS=1`. Ships `tau-packs.bin` as data slot 9 (owned, `required: false`).
* `tau-compat.json`: `min_omega` 0.6.0, `report_tags_max` 28, persist ids changed 10..33 (against v0.6.1).
* SHA-256 of both files in `SHA256SUMS.txt`. Not run on a Pocket as a package (host rehearsal of the release scripts; the files in it are those of DEV 185, which did run).
