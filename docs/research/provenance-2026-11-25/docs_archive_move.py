#!/usr/bin/env python3
"""Move the duplicate root copies and the whole-document-superseded docs listed in doc_inventory.csv to docs/archive/ (git mv, history kept),
add a one-line banner, rebase the relative Markdown links of the moved copy so no link breaks, and leave a small stub at the old path.
Run from the repo root once. Only rows with rule DUP-ROOT-IDENTICAL, DUP-ROOT-STALE or SUPERSEDED-WHOLE are touched."""
import csv, os, re, subprocess
LINK = re.compile(r'(?<!\!)(\[[^\]]*\]\()([^)\s]+)((?:\s+"[^"]*")?\))')
def rebase(text, old, new):
    def fix(m):
        t = m.group(2)
        if re.match(r'^([a-z][a-z0-9+.-]*:|#|/)', t): return m.group(0)
        path, _, frag = t.partition('#')
        if not path: return m.group(0)
        target = os.path.normpath(os.path.join(os.path.dirname(old), path))
        rel = os.path.relpath(target, os.path.dirname(new))
        return m.group(1) + rel + ('#' + frag if frag else '') + m.group(3)
    return LINK.sub(fix, text)
rows = list(csv.DictReader(open('docs/research/provenance-2026-11-25/doc_inventory.csv')))
moved = []
for r in rows:
    f, rule = r['path'], r['rule']
    if rule.startswith('DUP-ROOT-IDENTICAL') or rule.startswith('DUP-ROOT-STALE'):
        keep = rule.split('canonical ')[1]; bucket = 'docs/archive/duplicates-2026-09-reorg'
        banner = f'> ARCHIVED 2026-11-25: duplicate of the canonical copy [{keep}]({{rel_keep}}) (the 2026-09-27 reorg left the old copy here); kept for history.'
        why = 'duplicate'
    elif rule.startswith('SUPERSEDED-WHOLE'):
        keep = rule.split(': ')[1]; bucket = 'docs/archive/superseded-2026-11'
        banner = f'> ARCHIVED 2026-11-25: superseded by [{keep}]({{rel_keep}}); kept for history.'
        why = 'superseded'
    else: continue
    dest = os.path.join(bucket, os.path.basename(f))
    assert not os.path.exists(dest), dest
    os.makedirs(bucket, exist_ok=True)
    subprocess.run(['git', 'mv', f, dest], check=True)
    text = rebase(open(dest, errors='replace').read(), f, dest)
    rel_keep = os.path.relpath(keep, os.path.dirname(dest))
    open(dest, 'w').write(banner.replace('{rel_keep}', rel_keep) + '\n\n' + text)
    stub_rel_keep = os.path.relpath(keep, os.path.dirname(f)); stub_rel_arch = os.path.relpath(dest, os.path.dirname(f))
    title = os.path.basename(f)[:-3]
    open(f, 'w').write(f'# {title} (moved)\n\n> ARCHIVED 2026-11-25: this path is a stub so old links keep working.\n\n'
                        f'- Current document: [{keep}]({stub_rel_keep})\n- The old copy of this file, for history: [{dest}]({stub_rel_arch})\n')
    moved.append((f, dest, keep, why))
print(len(moved), 'moved')
with open('docs/research/provenance-2026-11-25/docs_moved.csv', 'w', newline='') as o:
    w = csv.writer(o); w.writerow(['old_path_now_stub', 'archived_copy', 'canonical_or_replacement', 'why']); w.writerows(moved)
