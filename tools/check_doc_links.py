#!/usr/bin/env python3
"""Dead-link checker for the Markdown documents.

Two kinds of reference are checked in every tracked *.md file (outside third_party/ and docs/archive/ banners only):
  1. Markdown links `[text](path)` whose target is a relative file or directory (URLs and #anchors skipped): HARD errors.
  2. Backticked repository paths (`docs/...`, `tools/...`, `sim/...`, `fw2/...`, `src/...`) that name a file that does not exist: SOFT
     (history documents quote paths of files that were later moved or deleted, so these are counted, not failed).

Usage:
  python3 tools/check_doc_links.py                      # exit 1 on any hard error (a broken Markdown link)
  python3 tools/check_doc_links.py --save FILE.json     # write the full list (hard + soft) as a baseline
  python3 tools/check_doc_links.py --baseline FILE.json # exit 1 if a hard error or a soft path is NEW against the baseline
  python3 tools/check_doc_links.py --baseline FILE.json --hard-only # only a new broken Markdown link fails (what make test-host runs)
  python3 tools/check_doc_links.py --root DIR           # check another tree (used by the test)
"""
import json, re, subprocess, sys
from pathlib import Path

LINK = re.compile(r'(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
TICK = re.compile(r'`((?:docs|tools|sim|fw2|fw|src|sdk|meters|themes|assets)/[A-Za-z0-9_./+-]+\.[A-Za-z0-9]+)`')
SKIP_PREFIX = ('third_party/',)


def tracked_md(root: Path):
    try:
        out = subprocess.run(['git', '-C', str(root), 'ls-files', '*.md'], capture_output=True, text=True, check=True).stdout.split('\n')
        files = [f for f in out if f]
        if files:
            return [f for f in files if (root / f).exists()]
    except Exception:
        pass
    return [str(p.relative_to(root)) for p in root.rglob('*.md') if '.git' not in p.parts]


def exists(root: Path, target: Path):
    """A path exists if it is there, or was moved verbatim to legacy-harpmudd/ (the mirror of the old location)."""
    if target.exists():
        return True
    try:
        rel = target.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return (root / 'legacy-harpmudd' / rel).exists()


def scan(root: Path):
    hard, soft = [], []
    for rel in tracked_md(root):
        if rel.startswith(SKIP_PREFIX):
            continue
        text = (root / rel).read_text(errors='replace')
        in_fence = False
        for n, line in enumerate(text.split('\n'), 1):
            if line.lstrip().startswith('```'):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            for m in LINK.finditer(line):
                t = m.group(1).split('#')[0]
                if not t or re.match(r'^[a-z][a-z0-9+.-]*:', t) or t.startswith('/'):
                    continue
                if not exists(root, (root / rel).parent.joinpath(t)):
                    hard.append(f'{rel}:{n}: {m.group(1)}')
            for m in TICK.finditer(line):
                p = m.group(1)
                if any(c in p for c in '*<>{}'):
                    continue
                if not exists(root, root / p) and not exists(root, (root / rel).parent.joinpath(p)):
                    soft.append(f'{rel}: {p}')
    return sorted(set(hard)), sorted(set(soft))


def main(argv):
    root = Path(argv[argv.index('--root') + 1]) if '--root' in argv else Path(__file__).resolve().parent.parent
    hard, soft = scan(root)
    if '--save' in argv:
        Path(argv[argv.index('--save') + 1]).write_text(json.dumps({'hard': hard, 'soft': soft}, indent=1) + '\n')
        print(f'check_doc_links: saved baseline: {len(hard)} broken Markdown links, {len(soft)} missing backticked paths')
        return 0
    if '--baseline' in argv:
        base = json.loads(Path(argv[argv.index('--baseline') + 1]).read_text())
        import os
        def tgt(h):
            rel, _, link = h.split(':', 2)[0], 0, h.split(': ', 1)[1]
            return os.path.normpath(os.path.join(os.path.dirname(rel), link.split('#')[0]))
        known_h = {tgt(h) for h in base['hard']}      # a link to a target that was already dead is not new (moved documents keep their old dead links)
        nh = [h for h in hard if tgt(h) not in known_h]
        # a missing path is new only if no document in the baseline named it (a moved document keeps its old stale references)
        known = {x.split(': ', 1)[1] for x in base['soft']}
        ns = [] if '--hard-only' in argv else [x for x in soft if x.split(': ', 1)[1] not in known]
        for x in nh: print('NEW BROKEN LINK:', x)
        for x in ns: print('NEW MISSING PATH:', x)
        print(f'check_doc_links: {len(nh)} new broken links, {len(ns)} new missing paths (baseline {len(base["hard"])} / {len(base["soft"])})')
        return 1 if (nh or ns) else 0
    for x in hard: print('BROKEN LINK:', x)
    print(f'check_doc_links: {len(hard)} broken Markdown links ({len(soft)} missing backticked paths, informational)')
    return 1 if hard else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
