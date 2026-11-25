#!/usr/bin/env python3
"""Classify every tracked Markdown document with evidence. Output: doc_inventory.csv. Run from the repo root.

Classes: CURRENT (true now, or in the session-start / protected set), REFERENCE (design or spec, still valid),
HISTORY (evidence, old handoffs and status), SUPERSEDED (a banner in the file says what replaced it),
DUPLICATE (same file name in docs/ and a subdirectory; canonical copy named), THIRD-PARTY (vendored licence or readme).
Evidence columns: last commit date, inbound references (other tracked text files that name the path or the unique base name),
whether CLAUDE.md / ROADMAP / CURRENT_STATUS / DECISIONS name it, the banner text, and the rule that decided the class.
"""
import csv, os, re, subprocess, sys, collections
def run(*a): return subprocess.run(a, capture_output=True, text=True).stdout
tracked = [f for f in run('git', 'ls-files').split('\n') if f]
mds = [f for f in tracked if f.endswith('.md') and not f.startswith('third_party/')]
# last commit date per file, one pass
last = {}
cur = None
for line in run('git', 'log', '--name-only', '--format=@@%cs').split('\n'):
    if line.startswith('@@'): cur = line[2:]
    elif line and line not in last: last[line] = cur
TEXT_EXT = ('.md', '.py', '.sh', '.json', '.c', '.h', '.inc', '.v', '.sv', '.vh', '.txt', '.csv', '.js', '.html', '.yml', '.yaml', '.tcl', '.qsf', '.sdc', '.qip')
texts = {}
for f in tracked:
    if f.endswith(TEXT_EXT) or f in ('Makefile',) :
        if f.startswith(('third_party/', 'docs/research/provenance-')): continue
        try: texts[f] = open(f, errors='replace').read()
        except OSError: pass
bases = collections.Counter(os.path.basename(f) for f in mds)
def inbound(f):
    b = os.path.basename(f); n = 0; anchors = set()
    for g, t in texts.items():
        if g == f: continue
        if f in t or (bases[b] == 1 and b in t):
            n += 1
            for a in ('CLAUDE.md', 'docs/ROADMAP.md', 'docs/CURRENT_STATUS.md', 'docs/DECISIONS.md'):
                if g == a: anchors.add(os.path.basename(a))
    return n, sorted(anchors)
START = {'CLAUDE.md', 'AGENTS.md', 'docs/CURRENT_STATUS.md', 'docs/ROADMAP.md', 'docs/AUDIT_TRAIL.md', 'docs/DECISIONS.md', 'docs/PROJECT_RULES.md', 'docs/CROSS_PROJECT_INTERFACE.md',
         'README.md', 'CHANGELOG.md', 'NOTICE.md', 'LICENSE', 'docs/PROVENANCE.md', 'docs/ATTRIBUTIONS.md', 'docs/CARD_INSTALL_PROCEDURE.md', 'docs/DEVELOPMENT_WORKFLOW.md', 'docs/DOC_MAP.md'}
# documents that Tau Omega (or the interop messages) link by path: never turned into a stub, never moved
PROTECTED = set()
for g, t in texts.items():
    if g.startswith('docs/interop/') or g.endswith('CROSS_PROJECT_INTERFACE.md'):
        PROTECTED |= set(re.findall(r'docs/[A-Za-z0-9_./-]+\.md', t))
# whole-document supersession decided by hand (a banner that only supersedes part of a file is not enough to archive it)
MANUAL_SUPERSEDED = {'docs/EQ_DESIGN.md': 'docs/features/CYMO_HALCYON_SPEC.md', 'docs/research/CODE_ORIGIN_ANALYSIS.md': 'docs/research/HARPMUDD_COMPARISON_AND_REWRITE_2026-10-12.md',
                     'docs/features/SDRAM_CONTRACT_V01_REVIEW.md': 'docs/features/SDRAM_CONTROLLER_CONTRACT.md'}
handoffs = sorted(f for f in mds if f.startswith('docs/handoffs/'))
newest_handoff = handoffs[-1] if handoffs else ''
# duplicates: docs/<name> that also exists in a subdirectory (not archive / interop); canonical = subdirectory copy (CLAUDE.md), unless the root copy is newer or has diverged and is newer
root = {os.path.basename(f): f for f in mds if f.count('/') == 1 and f.startswith('docs/')}
dup = {}   # path -> (kind, canonical)
for f in mds:
    b = os.path.basename(f)
    if f.startswith('docs/') and f.count('/') > 1 and b in root and not f.startswith(('docs/archive/', 'docs/interop/')) and b != 'README.md':
        r = root[b]; same = open(r, errors='replace').read() == open(f, errors='replace').read()
        if same or last.get(f, '') > last.get(r, ''):
            dup[r] = ('DUP-ROOT-STALE' if not same else 'DUP-ROOT-IDENTICAL', f)
        else:
            dup[r] = ('DUP-DIVERGED-ROOT-NEWER', f); dup[f] = ('DUP-DIVERGED-SUBDIR-OLDER', r)
SUP = re.compile(r'(supersed|replaced by|obsolete|no longer (current|used)|deprecated)', re.I)
rows = []
for f in sorted(mds):
    head = ''.join(open(f, errors='replace').readlines()[:14])
    ban = ''
    m = re.search(r'^[>*#\s]*[^\n]*(SUPERSEDED|superseded by|ARCHIVED|DEPRECATED|replaced by)[^\n]*', head, re.M | re.I)
    if m: ban = m.group(0).strip()[:220]
    n, anchors = inbound(f)
    rule = ''
    if f in START: cls, rule = 'CURRENT', 'session-start / protected set'
    elif f in PROTECTED and f in dup: cls, rule = 'DUPLICATE', f'DUP-PROTECTED-OMEGA-LINKS: Omega links this path, left in place; other copy {dup[f][1]}'
    elif f in MANUAL_SUPERSEDED and f not in PROTECTED: cls, rule = 'SUPERSEDED', 'SUPERSEDED-WHOLE: ' + MANUAL_SUPERSEDED[f]
    elif f in dup and dup[f][0].startswith('DUP-ROOT') and f not in PROTECTED: cls, rule = 'DUPLICATE', f'{dup[f][0]}: canonical {dup[f][1]}'
    elif f in dup: cls, rule = 'DUPLICATE', f'{dup[f][0]}: newer copy {dup[f][1]}'
    elif f.startswith('docs/archive/'): cls, rule = 'HISTORY', 'already in docs/archive/'
    elif f.startswith('docs/handoffs/'): cls, rule = ('CURRENT', 'newest handoff') if f == newest_handoff else ('HISTORY', 'older session handoff')
    elif f.startswith('docs/issues/'): cls, rule = 'CURRENT', 'docs/issues (open issues set)'
    elif f.startswith('docs/interop/'):
        cls, rule = ('HISTORY', 'interop rehearsal / lab drop record') if re.search(r'rehearsal|lab-drop', f) else ('CURRENT', 'interop with Omega (protected, Omega links here)')
    elif f.startswith('docs/tests/TEST_SCRIPT') or re.match(r'docs/TEST_SCRIPT', f): cls, rule = 'HISTORY', 'one-off Pocket test script for a past build'
    elif f.startswith('docs/research/provenance-'): cls, rule = 'HISTORY', 'dated provenance evidence'
    elif f.startswith('docs/research/') and re.search(r'20\d\d-\d\d-\d\d', f): cls, rule = 'HISTORY', 'dated research record'
    elif ban and re.search(r'supersed|ARCHIVED|replaced by', ban, re.I) and not f.startswith(('sdk/', 'docs/features/meters/', 'sim/')): cls, rule = 'REFERENCE', 'banner says PARTLY superseded: ' + ban[:80]
    elif f.startswith(('docs/features/', 'docs/memory/', 'docs/procedures/', 'docs/guide/', 'docs/schemas/', 'docs/diagrams/', 'docs/research/', 'sdk/', 'docs/vendor/')):
        cls = 'CURRENT' if (anchors and last.get(f, '') >= '2026-10-25') else 'REFERENCE'
        rule = 'design/spec/research; ' + ('named by a status doc and edited since 2026-10-25' if cls == 'CURRENT' else 'still valid unless a banner says otherwise')
    elif f.startswith('docs/'): cls, rule = ('CURRENT' if anchors and last.get(f, '') >= '2026-10-25' else 'REFERENCE'), 'docs root standing document'
    else: cls, rule = 'CURRENT', 'repository root / tooling readme'
    rows.append([f, cls, last.get(f, ''), n, ';'.join(anchors), ban, rule])
with open('docs/research/provenance-2026-11-25/doc_inventory.csv', 'w', newline='') as o:
    w = csv.writer(o); w.writerow(['path', 'class', 'last_commit', 'inbound_refs', 'named_by', 'banner', 'rule']); w.writerows(rows)
print(len(rows), collections.Counter(r[1] for r in rows))
print(collections.Counter(r[6].split(':')[0] for r in rows if r[1] in ('DUPLICATE',)))
for r in rows:
    if r[1] == 'SUPERSEDED': print(r[0], r[3], '|', r[5][:110])
