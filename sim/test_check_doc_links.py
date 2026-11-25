#!/usr/bin/env python3
"""tools/check_doc_links.py: a broken Markdown link fails, a good one passes, a new missing backticked path fails against a baseline."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / 'tools' / 'check_doc_links.py'
fails = 0
def check(name, ok):
    global fails
    print(('PASS ' if ok else 'FAIL ') + name)
    if not ok: fails += 1
def run(*a):
    r = subprocess.run([sys.executable, str(TOOL), *a], capture_output=True, text=True)
    return r.returncode, r.stdout
with tempfile.TemporaryDirectory() as t:
    r = Path(t); (r / 'docs').mkdir(); (r / 'tools').mkdir()
    (r / 'tools' / 'x.py').write_text('#\n')
    (r / 'docs' / 'a.md').write_text('# a\n[ok](b.md) [tool](../tools/x.py) [url](https://example.com) [anchor](#h) `tools/x.py`\n```\n[fenced](nope.md)\n```\n')
    (r / 'docs' / 'b.md').write_text('# b\n')
    rc, out = run('--root', str(r)); check('clean tree passes', rc == 0 and '0 broken' in out)
    (r / 'docs' / 'a.md').write_text((r / 'docs' / 'a.md').read_text() + '[dead](gone.md)\n')
    rc, out = run('--root', str(r)); check('a dead Markdown link fails (red)', rc == 1 and 'gone.md' in out)
    (r / 'docs' / 'a.md').write_text('# a\n[ok](b.md)\n')
    base = r / 'base.json'; rc, _ = run('--root', str(r), '--save', str(base)); check('baseline saved', rc == 0 and json.loads(base.read_text()) == {'hard': [], 'soft': []})
    (r / 'docs' / 'a.md').write_text('# a\n[ok](b.md) `docs/missing.md`\n')
    rc, out = run('--root', str(r), '--baseline', str(base)); check('a new missing backticked path fails against the baseline', rc == 1 and 'NEW MISSING PATH' in out)
    (r / 'docs' / 'a.md').write_text('# a\n[ok](b.md)\n')
    rc, out = run('--root', str(r), '--baseline', str(base)); check('no change passes against the baseline', rc == 0)
with tempfile.TemporaryDirectory() as t2:
    r = Path(t2); (r / 'tools').mkdir(); (r / 'legacy-harpmudd' / 'tools').mkdir(parents=True)
    (r / 'legacy-harpmudd' / 'tools' / 'old.py').write_text('#\n'); (r / 'a.md').write_text('see `tools/old.py` and [x](tools/old.py)\n')
    rc, out = run('--root', str(r)); check('a file moved verbatim to legacy-harpmudd/ still resolves', rc == 0)
print('FAILED' if fails else 'check_doc_links test: all passed')
sys.exit(1 if fails else 0)
