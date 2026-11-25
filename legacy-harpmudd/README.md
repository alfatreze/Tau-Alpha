# legacy-harpmudd/

Files whose text is almost entirely inherited from HarpMudd's MP3 player (Tau baseline `7ef8f0fb`, v1.4.0), that nothing live reads any more. Moved here on 2026-11-25 (phase 0 of the "remove HarpMudd-derived code" work) so they no longer sit in the working paths. Original paths are mirrored: `legacy-harpmudd/tools/xing_check.py` was `tools/xing_check.py`.

Why: provenance (see `docs/research/provenance-2026-11-25/harpmudd_manifest.csv`). Nothing here is built, tested or shipped. The moves are plain `git mv`, so history is kept (`git log --follow`).

How to restore one file: `git mv legacy-harpmudd/tools/xing_check.py tools/xing_check.py`.

How to compare with the baseline: `git diff 7ef8f0fb HEAD -- <original path>`; the measurement tool is `docs/research/provenance-2026-10-12/measure.py`.

Not deleted on purpose: deletion needs the owner's approval (D-T01). `docs/research/provenance-2026-11-25/removal.patch` (not applied) deletes this folder and the Pocket-confirmed scanout oracle.

Note: the licence and attribution wording (`NOTICE.md`, `docs/ATTRIBUTIONS.md`, `docs/PROVENANCE.md`) is not changed by moving files; that is for the owner to review.
