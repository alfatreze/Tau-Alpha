#!/usr/bin/env python3
"""Static, source-level regression guard for docs/features/AUDIO_FIRST_TRACK_LOAD_SPEC.md's
"already correct" invariants in fw/player.c and fw/art.inc.

WHY THIS EXISTS: investigating the "track loading blocks audio on cover-art decode" complaint found
that two of the three things a redesign would need are already true today, by construction, but
nowhere enforced -- a future refactor of load_track() (the natural next step of the redesign, see the
spec) could silently break either one without anyone noticing until it shipped and someone reported a
flashed/blanked cover or a hard click on every track. Rather than leave that as an unwritten fact,
this checks the actual source text for the three invariants the spec documents as already-satisfied
or load-bearing:

1. Ordering, in fw/player.c's load_track(): pcm_flush() (which starts the silent gap and arms the
   fade-in) runs BEFORE the cover-art decode block, which runs BEFORE prefill() (which starts filling
   the ring buffer prefill() drains from). This is not an accident -- fw/art.inc's own header comment
   states art decode must run "before prefill -- so the blocking reads it needs cannot starve the
   decoder, because playback has not started yet." A reorder that moves art decode after prefill()
   without also deferring it past load_track()'s return (the real fix the spec describes, not yet
   built) would silently reintroduce exactly the starvation that comment warns about.
2. fw/player.c's ui_loader_begin_ex() must NOT call ui_art_mount() -- B-075's fix for the "cover
   flashes to grey then back" bug relies on this function leaving the off-screen art stash alone so
   the OUTGOING track's cover keeps showing under the loading spinner. See its own inline comment.
3. fw/player.c's pcm_flush() must set fade_left to FADE_SAMPLES -- every discontinuity (a track load
   included) already arms the existing click-suppression ramp, which is what the spec's "fade in" ask
   turns out to already be satisfied by; if this line is ever removed, playback would resume click
   instead of fading in and nobody would notice from reading load_track() alone.

Usage: python3 tools/check_art_load_order.py [--check]  (both modes report and exit 1 on any
violation; --check is accepted for symmetry with this project's other generators)
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER = os.path.join(ROOT, "fw", "player.c")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def extract_function(src, name):
    """Return (start, end) character offsets of a C function's body (the braces themselves
    included), found by the DEFINITION (a short parameter list, no prose -- excludes comments that
    merely mention "name(...)" as English text, which a looser scan would greedily swallow up to
    some unrelated later '{'), then brace-matched to its close. Good enough for this file's own
    style (one definition per name, K&R braces, simple parameter lists); raises if not found or
    unbalanced."""
    # A real C parameter list here is short and made only of identifiers/*/,/spaces -- never a
    # sentence -- and '{' follows within a few characters (same line or the next, K&R style).
    m = re.search(r"\b" + re.escape(name) + r"\s*\([a-zA-Z_0-9\*, ]{0,40}\)\s*\n?\{", src)
    if not m:
        raise AssertionError(f"could not find a definition of {name}()")
    depth = 0
    i = src.index("{", m.start())
    start = i
    for j in range(i, len(src)):
        c = src[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return start, j + 1
    raise AssertionError(f"unbalanced braces scanning {name}()")


def strip_comments(text):
    """Remove /* ... */ and // ... comments so a search for a real call site does not match one
    mentioned only in English prose (this file's comments routinely spell out call names, e.g.
    B-075's own "NOT ui_art_mount() here" -- a naive text search would treat that as a real call)."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def check(verbose=True):
    src = read(PLAYER)
    problems = []

    # --- 1. load_track() ordering: pcm_flush() < art-decode block < prefill() ---
    ls, le = extract_function(src, "load_track")
    body = strip_comments(src[ls:le])
    flush_m = re.search(r"\bpcm_flush\s*\(\s*\)", body)
    art_m = re.search(r"\bart_decode\s*\(\s*audio_start\s*\)", body)
    prefill_m = re.search(r"\bprefill\s*\(\s*\)", body)
    if not (flush_m and art_m and prefill_m):
        problems.append(
            "load_track() is missing one of pcm_flush()/art_decode(audio_start)/prefill() -- "
            "the function this check expects has changed shape; update the check by hand."
        )
    else:
        if not (flush_m.start() < art_m.start() < prefill_m.start()):
            problems.append(
                "load_track() no longer runs pcm_flush() -> art decode -> prefill() in that order "
                "(found at chars %d/%d/%d within the function). fw/art.inc documents that art decode "
                "must precede prefill() so its SD reads cannot starve an already-primed FIFO -- if "
                "this was deliberately reordered, it must be paired with deferring the decode past "
                "load_track()'s return (see docs/features/AUDIO_FIRST_TRACK_LOAD_SPEC.md), not a bare "
                "swap." % (flush_m.start(), art_m.start(), prefill_m.start())
            )

    # --- 2. ui_loader_begin_ex() must not wipe the art stash ---
    ls2, le2 = extract_function(src, "ui_loader_begin_ex")
    body2 = strip_comments(src[ls2:le2])
    if re.search(r"\bui_art_mount\s*\(\s*\)", body2):
        problems.append(
            "ui_loader_begin_ex() now calls ui_art_mount() -- this wipes the off-screen art stash "
            "before the loader spinner is drawn, reintroducing the B-075 bug (the outgoing track's "
            "cover no longer shows under the spinner while the next track loads; see this function's "
            "own comment for the history)."
        )

    # --- 3. pcm_flush() must arm the fade-in ---
    ls3, le3 = extract_function(src, "pcm_flush")
    body3 = strip_comments(src[ls3:le3])
    if not re.search(r"\bfade_left\s*=\s*FADE_SAMPLES\b", body3):
        problems.append(
            "pcm_flush() no longer sets fade_left = FADE_SAMPLES -- every discontinuity (including "
            "every track load) used to arm the click-suppression ramp automatically; without this, "
            "playback resumes at full volume instantly instead of fading in."
        )

    if problems:
        for p in problems:
            print("FAIL:", p)
        return False
    if verbose:
        print("check_art_load_order: OK (%d invariants held)" % 3)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.parse_args()
    sys.exit(0 if check() else 1)


if __name__ == "__main__":
    main()
