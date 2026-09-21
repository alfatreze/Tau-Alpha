# Prompt to start the 0.4 media library task (paste into a new session)

```text
Continue the Tau Alpha project (Analogue Pocket openFPGA music player), working directory
/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha. This task: design and start the
0.4 MEDIA LIBRARY, moving the player away from playlists as the way to find music.

FIRST invoke the analogue-pocket-dev skill (Skill tool) and use it for anything about the Pocket,
APF, SDRAM/PSRAM, packaging or the SD card; consult its knowledge-base INDEX.local.md before relying
on a claim (my local entries: KB-029 PSRAM timing, KB-036 I/O register packing, KB-037/KB-040 PSRAM,
KB-041 accented names fail, KB-042 JPEG decode time follows file size).

THEN read, in order: docs/SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md (state, my rules, procedures, open
items), docs/MEDIA_LIBRARY_0.4_BRIEF.md (constraints, reusable parts, candidate design, phases, the
decisions I must make), CLAUDE.md, docs/ARCHITECTURE_ROADMAP.md (phases E and F), and the audit entries
B-021..B-029 in docs/AUDIT_TRAIL.md. Do not re-derive what they record.

STATE: Tau v0.3.0 is released (tag v0.3.0, local, not pushed): PSRAM in the bitstream, album-art buffer in
PSRAM, two zips (TAU and TAU_DIAGNOSTIC). The core cannot list directories, so the library needs a
host-built index; paths must be plain ASCII; on-chip RAM has no room to grow; PSRAM (32 MiB, 32/26 cycles)
is the natural home for the index; cover decode is slow for heavy JPEGs so browsing should use
pre-scaled thumbnails made by the sync tool (tools/sync_media.py is the starting point).

DO NOW (design only, no card, no VM, no code yet): (1) write docs/MEDIA_LIBRARY_0.4_SPEC.md: index and
thumbnail formats, size budget for a 7,180-track library, memory map, load and browse latency targets,
failure behaviour (feature off, not fallback), sync tool CLI, firmware plan behind a TAU_LIBRARY flag,
host tests, Pocket test plan with predictions written before hardware; (2) list the decisions from the
brief (section 5) with a recommendation for each and ask me the ones that change the design; (3) propose
the phases and what I will need to approve. Keep chat replies short: outcome first, key decisions, no
hashes/paths/code unless I ask; full detail goes in the docs.

RULES: I approve every VM launch and every SD-card write explicitly (stage, verify, then ask). Commit
only when I ask, with explicit git add paths, never other sessions' files; bash -n fw/build.sh and make
test-host/make test first. Log every change in docs/AUDIT_TRAIL.md (next free A-/B- id) and one line in
CLAUDE.md; hardware results carry an evidence label and predictions come first. Numbered test cores are
"TAU PSRAM NN" (never reused), remove superseded test cores on each install after a verified backup,
copy media to every test build with tools/sync_media.py. Every release is two zips (TAU and
TAU_DIAGNOSTIC) via tools/make_release.py, and the README diagnostics section stays current. Use zsh-safe
shell (quote paths, no unquoted multi-word variables).
```
